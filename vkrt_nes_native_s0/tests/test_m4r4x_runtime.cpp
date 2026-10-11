#include "vkrt/nes_native_s0/runtime.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace vkrt::nes::runtime;
namespace {
std::uint64_t checks=0,failures=0;
void check(bool ok,const char* name){++checks;if(!ok){++failures;if(failures<20)std::cerr<<"FAIL "<<name<<"\n";}}
void pacer_test(){
    RationalFramePacer p;
    constexpr std::uint64_t N=4000000;
    std::uint64_t sum=0;
    for(std::uint64_t i=0;i<N;++i){
        auto d=p.next_period_ns();sum+=d;
        const auto expected=((i+1)*kFramePeriodNumeratorNs)/kNtscFrameRateNumerator;
        check(sum==expected,"rational pacer exact cumulative phase");
    }
    check(p.frames()==N&&p.elapsed_ns()==sum,"pacer counters");
    p.reset();check(p.frames()==0&&p.elapsed_ns()==0,"pacer reset");
}
void ring_test(){
    SpscRing<std::uint32_t,1024> q;
    for(std::uint32_t i=0;i<1500000;++i){
        while(!q.push(i)){std::uint32_t x{};check(q.pop(x),"ring pop when full");}
        std::uint32_t x{};check(q.pop(x)&&x==i,"ring FIFO exact");
    }
    check(q.size_approx()==0,"ring empty");
    std::atomic<bool> bad=false;
    SpscRing<std::uint32_t,4096> concurrent;
    constexpr std::uint32_t N=500000;
    std::thread prod([&]{for(std::uint32_t i=0;i<N;++i){while(!concurrent.push(i))std::this_thread::yield();}});
    std::thread cons([&]{for(std::uint32_t i=0;i<N;++i){std::uint32_t x{};while(!concurrent.pop(x))std::this_thread::yield();if(x!=i)bad=true;}});
    prod.join();cons.join();check(!bad&&concurrent.size_approx()==0,"ring concurrent ordered SPSC");
}

void mailbox_test(){
    LatestMailbox<std::uint64_t> box;
    std::uint64_t seq=0,value=0;
    check(!box.consume_after(seq,value),"mailbox initially empty");
    constexpr std::uint64_t N=1000000;
    for(std::uint64_t i=1;i<=N;++i){
        const auto published=box.publish(i);
        check(published==i,"mailbox sequence exact");
        check(box.consume_after(seq,value)&&seq==i&&value==i,"mailbox latest exact");
        check(!box.consume_after(seq,value),"mailbox no duplicate consume");
    }
    std::atomic<bool> go=false,done=false,bad=false;
    LatestMailbox<std::uint64_t> concurrent;
    std::thread prod([&]{
        while(!go.load(std::memory_order_acquire))std::this_thread::yield();
        for(std::uint64_t i=1;i<=250000;++i)concurrent.publish(i);
        done.store(true,std::memory_order_release);
    });
    std::thread cons([&]{
        go.store(true,std::memory_order_release);
        std::uint64_t last=0,v=0,prev=0;
        while(!done.load(std::memory_order_acquire)||last<concurrent.sequence()){
            if(concurrent.consume_after(last,v)){
                if(v<prev||v!=last)bad.store(true,std::memory_order_relaxed);
                prev=v;
            }else std::this_thread::yield();
        }
        if(prev!=250000)bad.store(true,std::memory_order_relaxed);
    });
    prod.join();cons.join();
    check(!bad.load()&&concurrent.sequence()==250000,"mailbox concurrent latest-wins monotonic");
}

void replay_test(){
    ReplayTape t(std::string(64,'a'));
    constexpr std::uint32_t N=750000;
    std::uint32_t s=0x12345678u;
    for(std::uint32_t i=0;i<N;++i){
        s^=s<<13;s^=s>>17;s^=s<<5;
        t.append({std::uint8_t(s),std::uint8_t((i%9973)==0?kReplayWarmReset:0)});
    }
    const auto path=std::filesystem::temp_directory_path()/"vkrt_nes_m4r4x_runtime_test.vkrtrpl";
    t.save(path);
    auto u=ReplayTape::load(path);
    check(u.rom_sha256()==t.rom_sha256(),"replay ROM identity");
    check(u.frames().size()==t.frames().size(),"replay frame count");
    for(std::size_t i=0;i<t.frames().size();++i){
        check(u.frames()[i].buttons==t.frames()[i].buttons,"replay buttons byte exact");
        check(u.frames()[i].commands==t.frames()[i].commands,"replay commands byte exact");
    }
    check(u.fingerprint64()==t.fingerprint64(),"replay fingerprint stable");
    // Tamper one byte and require CRC rejection.
    {
        std::fstream f(path,std::ios::binary|std::ios::in|std::ios::out);
        f.seekg(100);char c{};f.read(&c,1);c^=0x55;f.seekp(100);f.write(&c,1);
    }
    bool rejected=false;try{(void)ReplayTape::load(path);}catch(...){rejected=true;}
    check(rejected,"replay tamper rejected");
    std::error_code ec;std::filesystem::remove(path,ec);
}
}
int main(){
    try{pacer_test();ring_test();mailbox_test();replay_test();}
    catch(const std::exception& e){++checks;++failures;std::cerr<<"FAIL exception "<<e.what()<<"\n";}
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<"\n";
    return failures?1:0;
}
