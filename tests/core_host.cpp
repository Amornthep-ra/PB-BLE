// Windows host adapter: exercise the production PB2 parser with real SHA/HMAC.
#include <windows.h>
#include <bcrypt.h>
#include <iostream>
#include <sstream>
#include <vector>
#include "../src/internal/PB2Protocol.h"
static bool digest(const uint8_t *key, size_t keySize, const uint8_t *p, size_t n, uint8_t *out) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
            key ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0) < 0) return false;
    bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0,
                const_cast<PUCHAR>(key), ULONG(keySize), 0) >= 0;
    if (ok) ok = BCryptHashData(hash, const_cast<PUCHAR>(p), ULONG(n), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, out, 32, 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}
static bool sha(const uint8_t *p, size_t n, uint8_t *out) { return digest(nullptr,0,p,n,out); }
static bool hmac(const uint8_t *k, const uint8_t *p, size_t n, uint8_t *out) { return digest(k,32,p,n,out); }
static uint8_t counter = 0;
static bool randomBytes(uint8_t *p, size_t n) {
    for(size_t i=0;i<n;++i) p[i]=counter++;
    return true;
}
int main() {
    PB2Protocol protocol(sha,hmac,randomBytes);
    PB2Stream stream;
    std::string line;
    while(std::getline(std::cin,line)) {
        if(line.compare(0,7,"CONFIG ")==0) {
            std::istringstream in(line.substr(7));
            std::string id,code,model;
            in>>id>>code>>model;
            std::cout<<(protocol.configure(id,code,model)?"YES":"NO");
        } else if(line=="RESET") {
            protocol.reset(); stream.reset(); std::cout<<"RESET";
        } else if(line.compare(0,7,"STREAM ")==0) {
            std::istringstream in(line.substr(7));
            uint32_t time; std::string hex;
            in >> time >> hex;
            for(size_t i=0;i+1<hex.size();i+=2) {
                uint8_t b=uint8_t(std::stoul(hex.substr(i,2),nullptr,16));
                stream.feed(b,time,
                    [&](const std::string &s) { std::cout<<protocol.line(s); },
                    [&](const uint8_t *p,size_t n,uint32_t) {
                        uint8_t payload[6]={}; std::cout<<protocol.packet(p,n,payload);
                    });
            }
        } else if(line=="READY") {
            std::cout<<(protocol.ready()?"YES":"NO");
        } else if(line.compare(0,7,"PACKET ")==0) {
            std::string hex=line.substr(7); std::vector<uint8_t> bytes;
            for(size_t i=0;i+1<hex.size();i+=2)
                bytes.push_back(uint8_t(std::stoul(hex.substr(i,2),nullptr,16)));
            uint8_t payload[6]={};
            std::cout<<protocol.packet(bytes.data(),bytes.size(),payload);
        } else {
            std::cout<<protocol.line(line);
        }
        std::cout<<std::endl;
    }
}


