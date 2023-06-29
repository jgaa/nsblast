#include "Replication.h"

#include "nsblast/logging.h"
#include "proto_util.h"

using namespace std;
using namespace std::string_literals;


namespace nsblast::lib {

void Replication::start()
{

}

Replication::Replication(Server &server)
    : server_{server}
{

}

GrpcPrimary::on_trxid_fn_t Replication::addAgent(GrpcPrimary::SyncClient &client, uint64_t fromTrxId)
{
    lock_guard lock{mutex_};

    auto agent = make_shared<Agent>(*this, client, fromTrxId);
    agents_[client.uuid()] = agent;

    return [agent](uint64_t trxId) {
        agent->onTrxId(trxId);
    };
}

Replication::Agent::Agent(Replication& parent, GrpcPrimary::SyncClient &client, uint64_t fromTrxId)
    : currentTrx{fromTrxId}, client_{client.weak_from_this()}, parent_{parent}
{

}

void Replication::Agent::iterateDb()
{
    auto trx = parent_.server().db().dbTransaction();

    const ResourceIf::RealKey key{currentTrx, ResourceIf::RealKey::Class::TRXID};

    auto fn = [this](ResourceIf::TransactionIf::key_t key, span_t value) {
        auto update = make_shared<grpc::nsblast::pb::SyncUpdate>();
        update->set_isinsync(false); // We are iterating, so not in sync (streaming).
        auto mtrx = update->mutable_trx();
        if (!mtrx->ParseFromArray(value.data(), value.size())) [[unlikely]] {
            LOG_ERROR << "Replication::Agent::iterateDb - Failed to deserialize the transaction "
                      << "for " << key;
        } else if (auto client = client_.lock()) {
            // enqueue will return false if the queue is full
            const auto trx_id = mtrx->id();
            const auto result =  client->enqueue(std::move(update));
            if (result) {
                currentTrx = trx_id;
                return true;
            }
        }
        return false;
    };

    trx->iterateFromPrevT(key, ResourceIf::Category::TRXLOG, fn);
}

void Replication::Agent::onTrxId(uint64_t trxId)
{
    lock_guard lock{mutex_};
    lastConfirmedTrx = trxId;

    if (lastConfirmedTrx == currentTrx) {
        if (state_ == State::ITERATING_DB) {
            // Try to switch to streaming
            parent_.applyIf(trxId, [this] {
                LOG_TRACE << "Replication::Agent::onTrxId - Agent "
                          << uuid() << " is switching to streaming updates.";
                state_ = State::STREAMING;
            });
        }
    }
}

boost::uuids::uuid Replication::Agent::uuid() const noexcept
{
    if (auto client = client_.lock()) {
        return client->uuid();
    }

    return {};
}


} // ns
