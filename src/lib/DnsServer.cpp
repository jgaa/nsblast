
#include <array>

#include "DnsServer.h"

#include "nsblast/logging.h"
#include "HttpServer.h"
#include "MessageHeader.h"
#include "LabelHeader.h"
#include "swagger_res.h"

using namespace std;
using namespace std;
using boost::asio::ip::tcp;
using boost::asio::ip::udp;
namespace net = boost::asio;            // from <boost/asio.hpp>
using namespace std::placeholders;
using namespace std::string_literals;
using namespace std::chrono_literals;

namespace nsblast::lib {

namespace ns {

std::atomic_uint64_t Request::next_id_{0};

Endpoint::Endpoint(DnsServer &parent, udp::endpoint ep)
    : socket_{parent.ctx()}, parent_{parent} {

    socket_.open(ep.protocol());
    socket_.set_option(boost::asio::socket_base::reuse_address(true));
    socket_.bind(ep);
}

void Endpoint::next()
{
    auto request = make_unique<ns::Request>(*this);

    auto r = request.get();
    assert(r);
    socket_.async_receive_from(r->queryBuffer(), r->sep(),
                               [this, request=move(request)](const boost::system::error_code& error,
                               std::size_t bytes_transferred) {

        if (error) {
            LOG_WARN << "DNS request on " << socket_.local_endpoint()
                      << " failed to receive data: " << error.message();

            // TODO: Don't let this become a vector for DoS
            this_thread::sleep_for(2ms); // Prevent tight loop.
        }

        // Don't recurse, and also allow any available thread to pick up the
        // next incoming request.
        parent_.ctx().post([this] {
            next();
        });

        if (!error) {
            try {
                process(*request, bytes_transferred);
            } catch (const exception& ex) {
                LOG_ERROR << "Caught exception on DNS::process on "
                          << socket_.local_endpoint()
                          << ": " << ex.what();
            }
        }
    });
}

void Endpoint::process(Request &req, const size_t len)
{
    // This is where the DNS server begins
    MessageHeader header(req.queryBuffer<string_view>());

    try {
        if (header.getQr() != 0) {
            throw InvalidQuery{"Not a question"};
        }

        LOG_TRACE << req.logName() << " Processing request " << header.getId();

        switch(header.getOpcode()) {
            case 0: // Standard query
                processQuestions(req, header);
                break;
            case 1: // Inverse query
            case 2: // Server status request
            default:
                createErrorReply(MessageHeader::Rcode::NOT_IMPLEMENTED,
                                 header, req);
        }
    } catch(const InvalidQuery& ex) {
        LOG_DEBUG << req.logName() << " Caught exception: " << ex.what();
        createErrorReply(MessageHeader::Rcode::FORMAT_ERROR, header, req);
    } catch(const UnknownDomain& ex) {
        /* This exception indicate that we are not authoritative for this
         * domain, so we will not reply.
         */
        LOG_DEBUG << req.logName()
                  << " Caught exception for domain we don't care about: "
                  << ex.what();
        throw;
    } catch(const UnknownSubDomain& ex) {
        LOG_DEBUG << req.logName() << " Caught exception: " << ex.what();
        // Unknown subdomain for a zone we own. Reply with error.
        createErrorReply(MessageHeader::Rcode::NAME_ERROR, header, req);
    } catch(const NoSoaRecord& ex) {
        LOG_DEBUG << req.logName() << " Caught exception NoSoaRecord: " << ex.what();
        createErrorReply(MessageHeader::Rcode::SERVER_FAILURE, header, req);
    } catch(const Refused& ex) {
        LOG_DEBUG << req.logName() << " Caught exception Refused: " << ex.what();
        createErrorReply(MessageHeader::Rcode::REFUSED, header, req);
        return;
    } catch(const LabelHeader::NoLabelsException& ex) {
        LOG_DEBUG << req.logName() << " Caught exception NoLabelsException: " << ex.what();
        createErrorReply(MessageHeader::Rcode::FORMAT_ERROR, header, req);
    } catch(const LabelHeader::IllegalLabelException& ex) {
        LOG_DEBUG << req.logName() << " Caught exception IllegalLabelException: " << ex.what();
        createErrorReply(MessageHeader::Rcode::FORMAT_ERROR, header, req);
        return;
    } catch(const std::exception& ex) {
        LOG_DEBUG << req.logName() << "Caught exception: " << ex.what();
        createErrorReply(MessageHeader::Rcode::SERVER_FAILURE, header, req);
    }
}

void Endpoint::processQuestions(Request &req, const MessageHeader &header)
{
    const auto [qbuf, qsize] = req.queryBuffer<pair<const void *, size_t>>();
    size_t bytes_left = qsize - header.getSize();

    LabelHeader::labels_t pointers;

    bool truncated = false; // If we run out of buffer space
    bool authoritative = true;
    uint16_t num_questions {0};
    uint16_t num_answers {0};
    uint16_t num_ns {0};
    uint16_t num_opt {0};

    AnswerBase::existing_labels_t existing_labels;
    zones_t authoritative_zones;
    zones_t ns_zones;

    // Reserve buffer-space for the message-header
    MessageHeader reply_hdr(header);
    replyBuffer.resize(reply_hdr.GetSize());

    LOG_TRACE << "Header: " << header.GetQdCount()
        << " questions ";

    /*! Since we use DNS header compression, we need to know the
        * offsets of the name-labels in the reply buffer. That means
        * that we have to copy all the question's into the buffer
        * before we start adding answers.
        * We therefore do a two-pass loop over the questions; first
        * to parse and copy them, and then to process them.
        */

    std::vector<Question> questions;
    for(uint16_t i = 0; i < header.GetQdCount(); ++i) {
        if (bytes_left == 0) {
            WAR_THROW_T(InvalidQuery, "Buffer underrun preparing next query");
        }

        const char *p = queryBuffer + (queryLen - bytes_left);

        questions.emplace_back<Question>(
            {p, bytes_left, pointers, queryBuffer,
                static_cast<uint16_t>(replyBuffer.size())});
        Question& question = questions.back();

        const auto qclass = question.GetQclass();
        if (qclass != CLASS_IN) {
            WAR_THROW_T(Refused, "Unsupported Qclass");
        }

        // Copy the question to the reply
        const size_t new_len = replyBuffer.size() + question.GetSize();
        if (new_len < max_reply_buffer_) {
            // Copy the original question unmodified
            const size_t len =  question.GetSize();
            std::copy(p, p + len, back_inserter(replyBuffer));
            ++num_questions;
        } else {
            WAR_THROW_T(InvalidQuery, "Too many or too thick questions!");
        }
    }

    // Process the questions and append the answers to the reply-buffer
    // until we are done or the buffer overflows (in which case the latest
    // answer will be rolled back and the truncated flag set).
    try {
        for(const Question& question : questions) {

            LOG_DEBUG << "Request ID " << header.GetId()
                << " asks about type " << question.GetQtype()
                << " class " << question.GetQclass()
                << " regarding " << log::Esc(question.GetDomainName());

            ProcessQuestion(question, replyBuffer, authoritative, num_answers,
                            existing_labels, authoritative_zones, ns_zones);
        }

        // Add NS references in the AUTH section
        for(const auto& z : authoritative_zones) {
            ProcessNsAuthZone(*z, num_ns, replyBuffer, existing_labels, ns_zones);
        }

        // Add IP addresse for NS servers we have named, and have data about
        for(const auto& z : ns_zones) {
            ProcessNsZone(*z, num_opt, replyBuffer, existing_labels);
        }

    } catch (const Truncated& ex) {
        LOG_WARN_FN << "Reply is truncated! " << ex;
        truncated = true;
    }

    // Write out the message-header
    WAR_ASSERT(replyBuffer.size() > reply_hdr.GetSize());
    reply_hdr.SetTc(truncated);
    reply_hdr.SetQr(true);
    reply_hdr.SetRa(false);
    reply_hdr.SetAa(authoritative);
    reply_hdr.SetQdCount(num_questions);
    reply_hdr.SetAnCount(num_answers);
    reply_hdr.SetNsCount(num_ns);
    reply_hdr.SetArCount(num_opt);
    reply_hdr.Write(&replyBuffer[0]);
}

void Endpoint::createErrorReply(MessageHeader::Rcode errCode,
                                const MessageHeader &hdr,
                                Request& req)
{
    MessageHeader reply_hdr(hdr);

    reply_hdr.setQr(true);
    reply_hdr.setRcode(errCode);
    reply_hdr.sesetAllCounters();

    auto [b, _] = req.replyBuffer<pair<void *, size_t>>();
    auto used = reply_hdr.write(b, req.replyBufferCapacity());
    req.setReplySize(used);

    LOG_DEBUG << req.logName() << " Query with ID " << hdr.getId()
            << " failed with error " << static_cast<int>(errCode);
}


} // ns

DnsServer::DnsServer(const Config &config)
    : config_{config}
{

}

std::future<void> DnsServer::start()
{
    udp::resolver resolver(ctx_);

    auto port = config_.dns_port;
    if (port.empty()) {
        port = "53";
    }

    LOG_DEBUG << "Preparing to use UDP  "
              << config_.dns_endpoint << " on "
              << " port " << port;


    auto endpoint = resolver.resolve({config_.dns_endpoint, port});
    udp::resolver::iterator end;
    for(; endpoint != end; ++endpoint) {
        udp::endpoint ep = endpoint->endpoint();
        LOG_INFO << "Starting DNS endpoint: " << ep;

        auto e = make_shared<ns::Endpoint>(*this, ep);
        e->next();
        endpoints_.emplace_back(e);
    }

}


};
