#include <iostream>
#include <string>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "lib/vcm/vcm.h"

using namespace vcm;

int main() {
    VCM vcm;

    if(vcm.init() == FAILURE) {
        std::cout << "Failed to initialize VCM\n";
        exit(-1);
    }

    std::cout << "device id: " << vcm.device << '\n';

    std::cout << "system endianness: ";
    if(vcm.sys_endianness == GSW_LITTLE_ENDIAN) {
        std::cout << "little endian\n";
    } else if(vcm.sys_endianness == GSW_BIG_ENDIAN) {
        std::cout << "big endian\n";
    }

    std::cout << "destination port: " << vcm.port << "\n";

    if(vcm.multicast_addr != 0) {
        struct in_addr addr;
        memset(&addr, 0, sizeof(addr));
        addr.s_addr = vcm.multicast_addr;
        std::cout << "multicast address: " << inet_ntoa(addr) << "\n";
    }

    for(std::string device : vcm.net_devices) {
        net_info_t* info = vcm.get_net(device);
        std::cout << "network device " + device + " id=" << info->unique_id;

        switch(info->mode) {
            case ADDR_AUTO:
                std::cout << " [auto:";
                std::cout << *((uint16_t*)(info->addr_info));
                std::cout << "]\n";

                break;
            case ADDR_STATIC:
                std::cout << " [static:";
                std::cout << "]\n";

                struct in_addr addr;
                memset(&addr, 0, sizeof(addr));
                addr.s_addr = *((uint32_t*)(info->addr_info));

                std::cout << inet_ntoa(addr) << "]\n";

                break;
        }
    }

    if(SUCCESS == vcm.parse_consts()) {
        std::cout << "\nconstants: \n";
        for(std::string c : vcm.constants) {
            std::string* s;
            s = vcm.get_const(c);

            std::cout << c << " = " << *s << "\n";
        }
    }


    size_t packet_count = 0;

    std::cout << "\npackets: \n";
    int i = 0;
    for(packet_info_t* packet : vcm.packets) {
        std::cout << i++ << ": ";
        std::cout << "size: " << packet->size << " ";
        std::cout << "port: " << packet->port;

        if(packet->is_virtual) {
            std::cout << " (virtual)";
        }

        std::cout << "\n";
        packet_count++;
    }

    std::cout << '\n';

    size_t meas_count = 0;

    for (auto& it : vcm.measurements) {
        std::cout << it << " ";

        measurement_info_t* info = vcm.get_info(it);
        if(info != NULL) {
            std::cout << info->size << " ";
            switch(info->type) {
                case UNDEFINED_TYPE:
                    std::cout << "undefined";
                    break;
                case INT_TYPE:
                    std::cout << "int";
                    break;
                case FLOAT_TYPE:
                    std::cout << "float";
                    break;
                case STRING_TYPE:
                    std::cout << "string";
                    break;
            }

            if(SIGNED_TYPE == info->sign) {
                std::cout << " signed";
            } else if(UNSIGNED_TYPE == info->sign) {
                std::cout << " unsigned";
            }

            if(GSW_LITTLE_ENDIAN == info->endianness) {
                std::cout << " little_endian";
            } else if(GSW_BIG_ENDIAN == info->endianness) {
                std::cout << " big_endian";
            }

            std::cout << "  [ ";
            for(location_info_t loc : info->locations) {
                std::cout << loc.packet_index << ":" << loc.offset << " ";
            }
            std::cout << "]";

            std::cout << '\n';
            meas_count++;
        }
    }
    std::cout << "\n" << meas_count << " measurements over " << packet_count << " packets\n";
}
