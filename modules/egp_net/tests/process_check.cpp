#include "net_core.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace egp::net;
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    Session session;
    const bool server = std::string(argv[1]) == "server";
    bool received = false;
    if (server) {
        if (session.listen(0,"127.0.0.1") != Result::Ok) return 3;
        std::vector<uint8_t> token;
        if (session.issue_token(9876,"127.0.0.1:"+std::to_string(session.statistics().local_port),token) != Result::Ok) return 4;
        // Test-only trusted local token handoff; this is not an authentication service.
        std::ofstream handoff(argv[2],std::ios::binary); handoff.write(reinterpret_cast<const char *>(token.data()),token.size()); handoff.close();
        uint64_t handle=0; if (session.spawn(7,{55,66},-1,handle) != Result::Ok) return 5;
        session.application_received=[&](int64_t peer,const std::vector<uint8_t> &payload) {
            if (payload==std::vector<uint8_t>({77}) && session.peers()[0].client_id==9876) {
                received = session.send(peer,{88},0,2,true)==Result::Ok;
            }
        };
    } else {
        std::ifstream handoff(argv[2],std::ios::binary);
        std::vector<uint8_t> token((std::istreambuf_iterator<char>(handoff)),std::istreambuf_iterator<char>());
        if(session.connect_token(9876,token)!=Result::Ok) return 6;
        session.application_received=[&](int64_t,const std::vector<uint8_t> &payload) { received=payload==std::vector<uint8_t>({88}); };
    }
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    auto complete=std::chrono::steady_clock::time_point::max();
    bool sent=false;
    while(std::chrono::steady_clock::now()<end) {
        if(session.pump()!=Result::Ok) return 7;
        if(!server && !sent && session.state()=="Connected" && session.entities().size()==1) {
            if(session.entities().begin()->second.state!=std::vector<uint8_t>({55,66})) return 8;
            sent=session.send(0,{77},0,2,true)==Result::Ok;
        }
        if(received && complete==std::chrono::steady_clock::time_point::max()) complete=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
        if(std::chrono::steady_clock::now()>=complete) {
            std::cout<<(server ? "EGP_PROCESS_SERVER_PASS" : "EGP_PROCESS_CLIENT_PASS")<<std::endl;
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return 9;
}
