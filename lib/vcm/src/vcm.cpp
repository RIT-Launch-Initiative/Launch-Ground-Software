#include "lib/vcm/vcm.h"
#include "lib/dls/dls.h"
#include "common/types.h"
#include <string>
#include <fstream>
#include <sstream>
#include <exception>

using namespace dls;
using namespace vcm;

VCM::VCM() {
    printf("testing123");
    MsgLogger logger("VCM", "Constructor");

    // default values
    addr = port = -1;
    protocol = PROTOCOL_NOT_SET;
    packet_size = 0;

    // figure out default config file
    char* env = getenv("GSW_HOME");
    if(env == NULL) {
        logger.log_message("GSW_HOME environment variable not set!");
        throw new std::runtime_error("Environment error in VCM");
    }
    config_file = env;
    config_file += "/";
    config_file += DEFAULT_CONFIG_FILE;

    // init
    if(init() != SUCCESS) {
        throw new std::runtime_error("Error in VCM init");
    }
}

VCM::VCM(std::string config_file) {
    this->config_file = config_file;

    // default values
    addr = port = -1;
    protocol = PROTOCOL_NOT_SET;
    packet_size = 0;

    // init
    if(init() != SUCCESS) {
        throw new std::runtime_error("Error in VCM init");
    }
}

VCM::~VCM() {
    // free all pointers in addr_map
    for(auto i : addr_map) {
        delete i.second;
    }
}

measurement_info_t VCM::get_info(std::string measurement) {
    return *(addr_map[measurement]);
}

RetType VCM::init() {
    MsgLogger logger;

    logger.log_message("init");

    std::ifstream f(config_file.c_str());
    if(!f) {
        logger.log_message("Failed to open config file: "+config_file);
        return FAILURE;
    }

    for(std::string line; std::getline(f,line); ) {
        if(line == "" || !line.rfind("#",0)) { // blank or comment '#'
            continue;
        }

        // get 1st + 2nd tokens
        std::istringstream ss(line);
        std::string fst;
        ss >> fst;
        std::string snd;
        ss >> snd;

        // port or addr or protocol line
        if(snd == "=") {
            std::string third;
            ss >> third;
            if(fst == "src") {
                try {
                    addr = std::stoi(third, NULL, 10);
                } catch(std::invalid_argument& ia) {
                    logger.log_message("Invalid address in line: " + line);
                    return FAILURE;
                }
            } else if(fst == "port") {
                try {
                    port = std::stoi(third, NULL, 10);
                } catch(std::invalid_argument& ia) {
                    logger.log_message("Invalid port in line: " + line);
                    return FAILURE;
                }
            } else if(fst == "protocol") {
                if(third == "udp") {
                    protocol = UDP;
                } else {
                    logger.log_message("Unrecogonized protocol on line: " + line);
                    return FAILURE;
                }
            }
        } else if(fst != "" && snd != "") {
            measurement_info_t* entry = new measurement_info_t;
            entry->addr = (void*)packet_size;
            try {
                entry->size = (size_t)(std::stoi(snd, NULL, 10));
            } catch(std::invalid_argument& ia) {
                logger.log_message("Invalid measurement size: " + line);
                return FAILURE;
            }
            packet_size += entry->size;
            addr_map[fst] = entry;
        } else {
            logger.log_message("Invalid line: " + line);
            return FAILURE;
        }
    }

    if(addr == -1 || port == -1 || protocol == PROTOCOL_NOT_SET) {
        logger.log_message("Config file missing addr, port, or protocol: " + config_file);
        return FAILURE;
    }

    return SUCCESS;
}