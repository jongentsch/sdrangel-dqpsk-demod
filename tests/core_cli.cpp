// SPDX-License-Identifier: GPL-3.0-or-later
#include "dqpskcore.h"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
int main(int argc,char** argv){
    if(argc<3){std::cerr<<"Usage: dqpsk_core_cli input-50ksps.cf32 output-labels.u8 [symbolRate] [carrierHz] [feedChunk]\n";return 2;}
    std::ifstream file; if(std::string(argv[1])!="-")file.open(argv[1],std::ios::binary);
    std::istream& in=std::string(argv[1])=="-"?std::cin:file;std::ofstream out(argv[2],std::ios::binary);
#ifdef _WIN32
    if(std::string(argv[1])=="-")_setmode(_fileno(stdin),_O_BINARY);
#endif
    if(!in||!out){std::cerr<<"Cannot open input/output\n";return 2;}
    dqpsk::Config config;if(argc>3)config.symbolRate=std::stod(argv[3]);if(argc>4)config.carrierOffset=std::stod(argv[4]);
    if(argc>6)config.rrcEnabled=std::stoi(argv[6])!=0;
    if(argc>7)config.pi4=std::stoi(argv[7])!=0;
    dqpsk::Core core(config);size_t chunk=argc>5?std::stoull(argv[5]):4096;
    if(chunk<1||chunk>1000000)return 2;std::vector<dqpsk::Complex> samples(chunk);
    uint64_t total=0,locked=0;
    while(in){in.read(reinterpret_cast<char*>(samples.data()),std::streamsize(samples.size()*sizeof(samples[0])));const auto bytes=in.gcount();
        if(bytes%sizeof(samples[0])){std::cerr<<"Truncated complex sample\n";return 3;}
        core.feed(samples.data(),size_t(bytes)/sizeof(samples[0]),[&](const dqpsk::Block& b){
            total+=b.labels.size();if(b.locked)locked+=b.labels.size();
            out.write(reinterpret_cast<const char*>(b.labels.data()),std::streamsize(b.labels.size()));
            std::cout<<"{\"sequence\":"<<b.sequence<<",\"count\":"<<b.labels.size()<<",\"firstSample\":"<<b.firstSample<<",\"locked\":"<<(b.locked?"true":"false")<<",\"fourthMoment\":"<<b.fourthMoment<<",\"phaseRMS\":"<<b.phaseRMSDegrees<<",\"amplitudeCV\":"<<b.amplitudeCV<<"}\n";
        });
    }
    std::cerr<<"Recovered "<<total<<" phase decisions; "<<locked<<" in locked blocks; "<<core.bufferedSamples()<<" trailing samples buffered\n";
    return out?0:4;
}
