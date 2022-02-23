#include "gtest/gtest.h"
#include "HttpServer.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

namespace {

class TestHandler : public RequestHandler {
public:
    TestHandler(bool expected) : expected_{expected} {}


    // RequestHandler interface
public:
    Response onReqest(const Request &req) override {
        if (!expected_) {
            return {418, "I am a tea pot!"};
        }
        return {};
    }

private:
    const bool expected_ = false;
};

template <typename T>
void add_some_routes(T& h) {
    h.addRoute("/", make_shared<TestHandler>(false));
    h.addRoute("/foo", make_shared<TestHandler>(false));
    h.addRoute("/test/me", make_shared<TestHandler>(true));
    h.addRoute("/test/me/index.html", make_shared<TestHandler>(true));
}

auto getRequest(string_view target) {
    return Request{string{target}, {}, {}, Request::Type::GET};
}

} // ns

TEST(testRoute, emptyRoute) {
    HttpServer h{{}};
    EXPECT_ANY_THROW(h.addRoute("", make_shared<TestHandler>(false)));
}

// Tests the unit-tests' assumptions
TEST(testRoute, returnsUnexpectedRoute) {
    HttpServer h{{}};
    add_some_routes(h);
    auto res = h.onRequest(getRequest("/foo"));
    EXPECT_EQ(res.code, 418);
}

TEST(testRoute, justSlash) {
    HttpServer h{{}};
    add_some_routes(h);
    h.addRoute("/", make_shared<TestHandler>(true));
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/")));
    EXPECT_EQ(res.code, 200);
}

TEST(testRoute, notFound1) {
    HttpServer h{{}};
    add_some_routes(h);
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/nothere")));
    EXPECT_EQ(res.code, 404);
}

TEST(testRoute, notFound2) {
    HttpServer h{{}};
    add_some_routes(h);
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/nothere/")));
    EXPECT_EQ(res.code, 404);
}

TEST(testRoute, notFound3) {
    HttpServer h{{}};
    add_some_routes(h);
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/nothere/either")));
    EXPECT_EQ(res.code, 404);
}

TEST(testRoute, notFound4) {
    HttpServer h{{}};
    add_some_routes(h);
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/nothere/either/")));
    EXPECT_EQ(res.code, 404);
}

TEST(testRoute, singleLeaf) {
    HttpServer h{{}};
    add_some_routes(h);
    h.addRoute("/teste", make_shared<TestHandler>(true));
    h.addRoute("/teste/foo", make_shared<TestHandler>(false));
    h.addRoute("/teste/foo/fofo", make_shared<TestHandler>(false));
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/teste")));
    EXPECT_EQ(res.code, 200);
}

TEST(testRoute, singleLeafWithSlash) {
    HttpServer h{{}};
    add_some_routes(h);
    h.addRoute("/teste", make_shared<TestHandler>(true));
    h.addRoute("/teste/foo", make_shared<TestHandler>(false));
    h.addRoute("/teste/foo/fofo", make_shared<TestHandler>(false));
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/teste/")));
    EXPECT_EQ(res.code, 200);
}

TEST(testRoute, dualLeaf) {
    HttpServer h{{}};
    add_some_routes(h);
    h.addRoute("/teste", make_shared<TestHandler>(false));
    h.addRoute("/teste/foo", make_shared<TestHandler>(true));
    h.addRoute("/teste/foo/fofo", make_shared<TestHandler>(false));
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/teste/foo")));
    EXPECT_EQ(res.code, 200);
}

TEST(testRoute, dualLeafWithSlash) {
    HttpServer h{{}};
    add_some_routes(h);
    h.addRoute("/teste", make_shared<TestHandler>(false));
    h.addRoute("/teste/foo", make_shared<TestHandler>(true));
    h.addRoute("/teste/foo/fofo", make_shared<TestHandler>(false));
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/teste/foo/")));
    EXPECT_EQ(res.code, 200);
}

TEST(testRoute, threeLeafs) {
    HttpServer h{{}};
    add_some_routes(h);
    h.addRoute("/teste", make_shared<TestHandler>(false));
    h.addRoute("/teste/foo", make_shared<TestHandler>(false));
    h.addRoute("/teste/foo/fofo", make_shared<TestHandler>(true));
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/teste/foo/fofo")));
    EXPECT_EQ(res.code, 200);
}

TEST(testRoute, threeLeafsWithSlash) {
    HttpServer h{{}};
    add_some_routes(h);
    h.addRoute("/teste", make_shared<TestHandler>(false));
    h.addRoute("/teste/foo", make_shared<TestHandler>(false));
    h.addRoute("/teste/foo/fofo", make_shared<TestHandler>(true));
    Response res;
    EXPECT_NO_THROW(res = h.onRequest(getRequest("/teste/foo/fofo/")));
    EXPECT_EQ(res.code, 200);
}

TEST(testFileHandler_resolve, relativeDir1) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("/test/a/b/c/../../../me");
    EXPECT_EQ(r, "/tmp/root/test/me");
}

TEST(testFileHandler_resolve, relativeDir2) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("/../../tmp/root");
    EXPECT_EQ(r, "/tmp/root");
}

TEST(testFileHandler_resolve, relativeDir3) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("foo");
    EXPECT_EQ(r, "/tmp/root/foo");
}

TEST(testFileHandler_resolve, relativeDir4) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("foo/");
    EXPECT_EQ(r, "/tmp/root/foo");
}

TEST(testFileHandler_resolve, absoluteDir1) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("/foo");
    EXPECT_EQ(r, "/tmp/root/foo");
}

TEST(testFileHandler_resolve, absoluteDir2) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("/foo/");
    EXPECT_EQ(r, "/tmp/root/foo");
}

TEST(testFileHandler_resolve, absoluteDir3) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("/foo/bar");
    EXPECT_EQ(r, "/tmp/root/foo/bar");
}

TEST(testFileHandler_resolve, absoluteDir4) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("/foo/bar/");
    EXPECT_EQ(r, "/tmp/root/foo/bar");
}

TEST(testFileHandler_resolve, root1) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("/");
    EXPECT_EQ(r, "/tmp/root");
}

TEST(testFileHandler_resolve, root2) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("./");
    EXPECT_EQ(r, "/tmp/root");
}

TEST(testFileHandler_resolve, root3) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve("/.");
    EXPECT_EQ(r, "/tmp/root");
}

TEST(testFileHandler_resolve, root4) {
    HttpServer::FileHandler fh{"/tmp/root"};
    auto r = fh.resolve(".");
    EXPECT_EQ(r, "/tmp/root");
}

TEST(testFileHandler_resolve, relativeEscapeFs1) {
    HttpServer::FileHandler fh{"/tmp/root"};
    EXPECT_ANY_THROW(fh.resolve("/.."));
}

TEST(testFileHandler_resolve, relativeEscapeFs2) {
    HttpServer::FileHandler fh{"/tmp/root"};
    EXPECT_ANY_THROW(fh.resolve(".."));
}


TEST(testFileHandler_resolve, relativeEscapeFs3) {
    HttpServer::FileHandler fh{"/tmp/root"};
    EXPECT_ANY_THROW(fh.resolve("/test/../test/../foo/../../bar"));
}



int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
