/*******************************************************************************
* Name: TelemetryViewer.cpp
*
* Purpose: Top level access to telemetry measurements
*
* Author: Will Merges
*
* RIT Launch Initiative
*******************************************************************************/
#include <string.h>
#include <limits.h>
#include "lib/telemetry/TelemetryViewer.h"
#include "lib/dls/dls.h"
#include "lib/convert/convert.h"

using namespace dls;
using namespace convert;


TelemetryViewer::TelemetryViewer() {
    update_mode = STANDARD_UPDATE;
    packet_ids = NULL;
    num_packets = 0;
    vcm = NULL;
    packet_sizes = NULL;
    packet_buffers = NULL;
}

TelemetryViewer::~TelemetryViewer() {
    if(packet_ids != NULL) {
        delete packet_ids;
    }

    if(packet_buffers != NULL) {
        for(size_t i = 0; i < num_packets; i++) {
            if(packet_buffers[i] != NULL) {
                delete packet_buffers[i];
            }
        }

        delete packet_buffers;
    }

    if(packet_sizes != NULL) {
        delete packet_sizes;
    }
}

RetType TelemetryViewer::init() {
    VCM vcm; // default VCM, leave on stack after init
    return init(&vcm);
}

RetType TelemetryViewer::init(VCM* vcm) {
    MsgLogger logger("TelemetryViewer", "init");

    if(shm.init(vcm) == FAILURE) {
        logger.log_message("failed to initialize telemetry shared memory");
        return FAILURE;
    }

    if(shm.open() == FAILURE) {
        logger.log_message("failed to attach to telemetry shared memory");
        return FAILURE;
    }

    this->vcm = vcm;

    packet_ids = new unsigned int[vcm->num_packets];
    packet_buffers = new uint8_t*[vcm->num_packets];

    return SUCCESS;
}

void TelemetryViewer::remove_all() {
    check_all = false;
    num_packets = 0;

    // TODO free packet_buffers? currently leaves allocated until destruction
}

RetType TelemetryViewer::add_all() {
    check_all = true;

    // NOTE: this relies on the fact packet ids are sequential indices
    // we still want to add these so that we create the necessary buffers etc.
    for(unsigned int id = 0; id < vcm->num_packets; id++) {
        if(FAILURE == add(id)) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

RetType TelemetryViewer::add(std::string measurement) {
    MsgLogger logger("TelemetryViewer", "add");

    measurement_info_t* info = vcm->get_info(measurement);

    for(location_info_t loc : info->locations) {
        if(FAILURE == add(loc.packet_index)) {
            logger.log_message("failed to add measurement: " + measurement);
            return FAILURE;
        }
    }

    return SUCCESS;
}

RetType TelemetryViewer::add(unsigned int packet_id) {
    if(packet_id < vcm->num_packets) {
        for(size_t i = 0; i < num_packets; i++) {
            if(packet_ids[i] == packet_id) {
                // packet is already being tracked
                return SUCCESS;
            }
        }

        packet_ids[num_packets] = packet_id;
        packet_sizes[packet_id] = vcm->packets[packet_id]->size;
        packet_buffers[packet_id] = new uint8_t[packet_sizes[packet_id]];
        num_packets++;

        return SUCCESS;
    }

    MsgLogger logger("TelemetryViewer", "add");
    logger.log_message("invalid packet id");
    return FAILURE;
}

void TelemetryViewer::set_update_mode(update_mode_t mode) {
    update_mode = mode;

    TelemetryShm::read_mode_t shm_mode;
    switch(update_mode) {
        case STANDARD_UPDATE:
            shm_mode = TelemetryShm::STANDARD_READ;
            break;
        case BLOCKING_UPDATE:
            shm_mode = TelemetryShm::BLOCKING_READ;
            break;
        case NONBLOCKING_UPDATE:
            shm_mode = TelemetryShm::NONBLOCKING_READ;
            break;
    }

    shm.set_read_mode(shm_mode);
}

RetType TelemetryViewer::update() {
    MsgLogger logger("TelemetryViewer", "update");

    RetType status;
    if(check_all) {
        status = shm.read_lock();
        if(status != SUCCESS) {
            return status; // could be BLOCKED or FAILURE
        }
    } else {
        while(1) {
            status = shm.read_lock(packet_ids, num_packets);
            if(status != SUCCESS) {
                return status; // could be BLOCKED or FAILURE
            }

            // need to check that one of our packets did actually update if we're in blocking mode
            // with less than 32 packets we can skip this since we can guarantee we were awoken for our packet
            // this is because our packet index isn't equivalent to another packet modulo 32 and the lock only allows 32 bits
            if(update_mode == BLOCKING_UPDATE && vcm->num_packets > 32) {
                bool updated = false;
                for(size_t i = 0; i < num_packets && !updated; i++) {
                    if(FAILURE == shm.packet_updated(packet_ids[i], &updated)) {
                        logger.log_message("failed to determine if packet was updated");
                        return FAILURE;
                    }

                    if(updated) {
                        // we found a packet that did indeed update
                        break;
                    }
                }
            } else {
                break;
            }
        } // end while
    }

    // copy in packets we're tracking from shared memory
    unsigned int id;
    for(size_t i = 0; i < num_packets; i++) {
        id = packet_ids[i];
        memcpy(packet_buffers[id], shm.get_buffer(id), packet_sizes[id]);
    }

    // unlock shared memory
    if(FAILURE == shm.read_unlock()) {
        logger.log_message("failed to unlock shared memory");
        return FAILURE;
    }

    return SUCCESS;
}

RetType TelemetryViewer::latest_data(measurement_info_t* meas, uint8_t** data) {
    MsgLogger logger("TelemetryViewer", "latest_data");

    std::vector<location_info_t> locs = meas->locations;

    if(locs.size() <= 0) {
        logger.log_message("measurement does not exist anywhere");
        return FAILURE;
    }

    location_info_t* best_loc;
    long int best = INT_MAX;
    uint32_t curr;
    for(size_t i = 0; i < locs.size(); i++) {
        if(shm.update_value(locs[i].packet_index, &curr) == FAILURE) {
            logger.log_message("unable to retrieve update value for packet");
            return FAILURE;
        }

        if(curr < best) {
            best = curr;
            best_loc = &(locs[i]);
        }
    }

    // guaranteed loc is not NULL since we found some location
    *data = packet_buffers[best_loc->packet_index] + best_loc->offset;

    return SUCCESS;
}

RetType TelemetryViewer::get_str(std::string measurement, std::string* val) {
    MsgLogger logger("TelemetryViewer", "get_str");

    measurement_info_t* meas = vcm->get_info(measurement);
    if(meas == NULL) {
        logger.log_message("measurement " + measurement + " was not found");
        return FAILURE;
    }

    uint8_t* data;
    if(latest_data(meas, &data) == FAILURE) {
        logger.log_message("failed to locate latest data for measurement " + measurement);
        return FAILURE;
    }

    // call convert library with 'data'

    return SUCCESS;

}
