
#include <hobbes/hobbes.H>
#include <hobbes/ipc/net.H>
#include <hobbes/net.H>
#include "test.H"

#include <mutex>
#include <thread>
#include <condition_variable>

using namespace hobbes;
static cc& c() { static cc x; return x; }

// start a basic server to validate RPC communication
static int serverPort = -1;
static std::mutex serverMtx;
static std::condition_variable serverStartup;

static void runTestServer(int ps, int pe) {
  std::unique_lock<std::mutex> lk(serverMtx);
  serverPort = ps;
  while (serverPort < pe) {
    try {
      installNetREPL(serverPort, &c());
      lk.unlock();
      serverStartup.notify_one();
      runEventLoop();
    } catch (std::exception&) {
      ++serverPort;
    }
  }
  serverPort = -1;
}

int testServerPort() {
  if (serverPort < 0) {
    std::unique_lock<std::mutex> lk(serverMtx);
    std::thread serverProc(std::bind(&runTestServer, 8765, 9500));
    serverProc.detach();
    serverStartup.wait(lk);
    if (serverPort < 0) throw std::runtime_error("Couldn't allocate port for test server");
  }
  return serverPort;
}

/**************************
 * types/data for net communication
 **************************/
typedef std::map<std::string, size_t> NameCounts;
typedef std::pair<std::string,size_t> NC;
bool operator==(const NameCounts& ncs, const std::vector<NC>& ncsc) {
  for (const auto& nc : ncsc) { auto i = ncs.find(nc.first); if (i == ncs.end() || i->second != nc.second) { return false; } } return ncs.size() == ncsc.size();
}

std::ostream& operator<<(std::ostream& os, const NC& nc) {
  os << "(\"" << nc.first << "\", " << nc.second << ")";
  return os;
}
template <typename T>
  std::ostream& operator<<(std::ostream& os, const std::vector<T>& xs) {
    os << "["; if (xs.size() > 0) { os << xs[0]; for (size_t i = 1; i < xs.size(); ++i) os << ", " << xs[i]; } os << "]";
    return os;
  }
std::ostream& operator<<(std::ostream& os, const NameCounts& ncsc) {
  os << "{"; auto nc = ncsc.begin(); if (nc != ncsc.end()) { os << "\"" << nc->first << "\" => " << nc->second; ++nc; for(;nc != ncsc.end();++nc) os << ", " << "\"" << nc->first << "\" => " << nc->second; } os << "}";
  return os;
}

DEFINE_HNET_ENUM(Kid, (Jim), (Bob));

DEFINE_HNET_STRUCT(
  Group,
  (std::string, id),
  (Kid,         kid),
  (double,      aa),
  (size_t,      bb)
);
typedef std::vector<Group> Groups;
std::ostream& operator<<(std::ostream& os, const Group& g) { os << "{id=\"" << g.id << "\", kid=" << ((g.kid == Kid::Jim()) ? "|Jim|" : "|Bob|") << ", aa=" << g.aa << ", bb=" << g.bb << "}"; return os; }

DEFINE_HNET_VARIANT(
  V,
  (Bob, int),
  (Frank, std::string),
  (Nothing, hobbes::net::unit)
);
struct showV : public VVisitor<void> { std::ostream& os; showV(std::ostream& os) : os(os) { }
  void Bob(const int& x) const { os << "Bob=" << x; }
  void Frank(const std::string& x) const { os << "Frank=\"" << x << "\""; }
  void Nothing(const hobbes::net::unit& u) const { os << "Nothing=()"; }
};
std::ostream& operator<<(std::ostream& os, const V& v) { os << "|"; v.visit(showV(os)); os << "|"; return os; }

// make sure that enums with custom ctor IDs transfer correctly
enum class CustomIDEnum : uint32_t {
  Red = 55,
  Green = 12,
  Blue  = 257
};
std::ostream& operator<<(std::ostream& os, const CustomIDEnum& e) {
  switch (e) {
  case CustomIDEnum::Red:   os << "|Red|";   break;
  case CustomIDEnum::Green: os << "|Green|"; break;
  case CustomIDEnum::Blue:  os << "|Blue|";  break;
  default:                  os << "???";     break;
  }
  return os;
}

namespace hobbes { namespace net {
template <>
  struct io<CustomIDEnum> {
    typedef void can_memcpy;
    static void encode(bytes* out) {
      w(_HNET_TYCTOR_VARIANT, out);
      w((size_t)3, out);
      ws("Red",   out); w((uint32_t)(CustomIDEnum::Red),   out); encode_primty("unit", out);
      ws("Green", out); w((uint32_t)(CustomIDEnum::Green), out); encode_primty("unit", out);
      ws("Blue",  out); w((uint32_t)(CustomIDEnum::Blue),  out); encode_primty("unit", out);
    }

    static std::string describe()                          { return "|Red,Green,Blue|"; }
    static size_t      size(const CustomIDEnum&)           { return sizeof(CustomIDEnum); }
    static void        write(int s, const CustomIDEnum& x) { io<uint32_t>::write(s, (uint32_t)x); }
    static void        read(int s, CustomIDEnum* x)        { io<uint32_t>::read(s, (uint32_t*)x); }

    typedef io<uint32_t>::async_read_state async_read_state;
    static void prepare(async_read_state* o) { io<uint32_t>::prepare(o); }
    static bool accum(int s, async_read_state* o, CustomIDEnum* x) { return io<uint32_t>::accum(s, o, (uint32_t*)x); }
  };
}}

/**************************
 * the synchronous client networking API
 **************************/
DEFINE_NET_CLIENT(
  SyncClient,
  (add,     int(int,int),                   "\\x y.x+y"),
  (doit,    std::string(),                  "\\().\"missiles launched\""),
  (misc,    NameCounts(std::string,size_t), "\\n c.[(n++\"_\"++show(i), i) | i <- [0L..c]]"),
  (grpv,    V(Group),                       "\\_.|Frank=\"frank\"|"),
  (nothing, V(),                            "\\_.|Nothing=()|"),
  (recover, Groups(int,int),                "\\i e.[{id=\"group_\"++show(k),kid=|Jim|,aa=convert(k),bb=convert(k)}|k<-[i..e]]"),
  (eidv,    CustomIDEnum(CustomIDEnum),     "id")
);

TEST(Net, syncClientAPI) {
  SyncClient c("localhost", testServerPort());
  EXPECT_EQ(c.add(1,2), 3);
  EXPECT_EQ(c.doit(), "missiles launched");
  EXPECT_EQ(c.misc("foo", 5), list(NC("foo_0",0),NC("foo_1",1),NC("foo_2",2),NC("foo_3",3),NC("foo_4",4),NC("foo_5",5)));

  Group grp = { "id", Kid::Jim(), 4.2, 42 };
  EXPECT_EQ(grp, grp);
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(c.grpv(grp), V::Frank("frank"));
  }
  EXPECT_EQ(c.nothing(), V::Nothing(hobbes::net::unit()));

  Groups ros = { {"group_0",Kid::Jim(),0.0,0},{"group_1",Kid::Jim(),1.0,1},{"group_2",Kid::Jim(),2.0,2},{"group_3",Kid::Jim(),3.0,3},{"group_4",Kid::Jim(),4.0,4} };
  EXPECT_EQ(c.recover(0,4), ros);

  EXPECT_EQ(c.eidv(CustomIDEnum::Green), CustomIDEnum::Green);
}

/**************************
 * the asynchronous client networking API
 **************************/
DEFINE_ASYNC_NET_CLIENT(
  AsyncClient,
  (add,     int(int,int),                   "\\x y.x+y"),
  (doit,    std::string(),                  "\\().\"missiles launched\""),
  (misc,    NameCounts(std::string,size_t), "\\n c.[(n++\"_\"++show(i), i) | i <- [0L..c]]"),
  (grpv,    V(Group),                       "\\_.|Frank=\"frank\"|"),
  (nothing, V(),                            "\\().|Nothing=()|"),
  (recover, Groups(int,int),                "\\i e.[{id=\"group_\"++show(k),kid=|Jim|,aa=convert(k),bb=convert(k)}|k<-[i..e]]"),
  (eidv,    CustomIDEnum(CustomIDEnum),     "id")
);
void stepAsyncClient(int, void* p) { ((AsyncClient*)p)->step(); }

TEST(Net, asyncClientAPI) {
  AsyncClient c("127.0.0.1", "127.0.0.1", testServerPort());
  c.add(1,2, [](int r) { EXPECT_EQ(r, 3); });
  c.doit([](const std::string& r) { EXPECT_EQ(r, "missiles launched") });
  c.misc("foo", 5, [](const NameCounts& ncs) { EXPECT_EQ(ncs, list(NC("foo_0",0),NC("foo_1",1),NC("foo_2",2),NC("foo_3",3),NC("foo_4",4),NC("foo_5",5))); });

  Group grp = { "id", Kid::Jim(), 4.2, 42 };
  for (size_t i = 0; i < 1000; ++i) {
    c.grpv(grp, [](const V& r) { EXPECT_EQ(r, V::Frank("frank")); });
    c.nothing([](const V& r  ) { EXPECT_EQ(r, V::Nothing(hobbes::net::unit())); });
  }

  Groups ros = { {"group_0",Kid::Jim(),0.0,0},{"group_1",Kid::Jim(),1.0,1},{"group_2",Kid::Jim(),2.0,2},{"group_3",Kid::Jim(),3.0,3},{"group_4",Kid::Jim(),4.0,4} };
  c.recover(0,4, [&](const Groups& r) { EXPECT_EQ(r, ros); });
  
  c.eidv(CustomIDEnum::Green, [](const CustomIDEnum& x) { EXPECT_EQ(x, CustomIDEnum::Green); });

  // run a quick epoll loop to process results asynchronously (expect all results within 30 seconds)
  registerEventHandler(c.fd(), &stepAsyncClient, &c);
  for (size_t s = 0; s < 30 && c.pendingRequests() > 0; ++s) {
    runEventLoop(1000*1000);
  }
  EXPECT_EQ(c.pendingRequests(), 0);
}

