/********************************************************************
*  Name: sensors.cpp
*
*  Purpose: Functions relating to data acquisition sensors
*
*  Author: Will Merges
*
*  RIT Launch Initiative
*********************************************************************/
#include "lib/daq/sensors.h"
#include "lib/telemetry/TelemetryViewer.h"
#include "lib/telemetry/TelemetryWriter.h"
#include "lib/trigger/trigger.h"
#include "common/types.h"

using namespace trigger;

// macros
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)


RetType DAQ_ADC_SCALE(TelemetryViewer* tv, TelemetryWriter* tw, arg_t* args) {
    // NOTE: we don't do an arg check!
    // if it segfaults, that's the fault of whoever made the triggers file wrong

    int32_t data;

    if(unlikely(SUCCESS != tv->get_int(args->args[0], &data))) {
        return FAILURE;
    }

    static const double vref = 2.442; // volts

    double result = (double)data * vref / (double)(1 << 23);

    if(unlikely(tw->write(args->args[1], (uint8_t*)&result, sizeof(double)))) {
        return FAILURE;
    }

    return SUCCESS;
}

// K inverse polynomial approximations from NIST
// https://srdata.nist.gov/its90/type_k/kcoefficients_inverse.html
// given in 3 temperature ranges

// polynomial coefficients for range 0, -5.891mV to 0mV
#define K_INVERSE_R0_NUM 9 // number of coefficients
static double dr0[K_INVERSE_R0_NUM] =
{
    0.0E0,          // d0
    2.5173462E1,    // d1
    -1.1662878E0,   // d2
    -1.0833638E0,   // d3
    -8.9773540E-1,  // d4
    -3.7342377E-1,  // d5
    -8.6632643E-2,  // d6
    -1.0450598E-2,  // d7
    -5.1920577E-4   // d8
};

// polynomial coefficients for range 1, 0mV to 20.644mV
#define K_INVERSE_R1_NUM 10 // number of coefficients
static double dr1[K_INVERSE_R1_NUM] =
{
    0.0E0,          // d0
    2.508355E1,     // d1
    7.860106E-2,    // d2
    -2.503131E-1,   // d3
    8.315270E-2,    // d4
    -1.228034E-2,   // d5
    9.804036E-4,    // d6
    -4.413030E-5,   // d7
    1.057734E-6,    // d8
    -1.052755E-8    // d9
};

// polynomial coefficients for range 2, 20.644mV to 54.886mV
#define K_INVERSE_R2_NUM 7 // number of coefficients
static double dr2[K_INVERSE_R2_NUM] =
{
    -1.318058E-2,   // d0
    4.830222E1,     // d1
    -1.646031E0,    // d2
    5.464731E2,     // d3
    -9.650715E-4,   // d4
    8.802193E-6,    // d5
    -3.110810E-8,   // d6
};

// @arg1 raw input (32 bit int)
// @arg2 connected status
// @arg3 remote temperature in Celsius (double)
// @arg4 ambient temperature in Celsius (double)
RetType KTYPE_THERMOCOUPLE(TelemetryViewer* tv, TelemetryWriter* tw, arg_t* args) {
    // NOTE: we don't do an arg check
    // if it segfaults, that's the fault of whoever made the triggers file wrong

    uint32_t data;

    if(unlikely(SUCCESS != tv->get_uint(args->args[0], &data))) {
        return FAILURE;
    }

    uint8_t connected = 1;

    if(data & (1 << 15)) {
        connected = 0;
    }

    if(unlikely(SUCCESS != tw->write(args->args[1], (uint8_t*)&connected, sizeof(uint8_t)))) {
        return FAILURE;
    }

    if(!connected) {
        return FAILURE;
    }

    // remote temp, 14 highest bits
    uint16_t tr = data >> 18;
    int16_t TR = tr;
    if(tr & 0b0010000000000000) { // sign bit is set
        TR *= -1;
    }

    double remote = TR * 0.25;

    if(unlikely(SUCCESS != tw->write(args->args[2], (uint8_t*)&remote, sizeof(double)))) {
        return FAILURE;
    }

    // ambient temp
    uint16_t tamb = (data >> 4) & 0xF0;
    int16_t TAMB = tamb;
    if(tamb & 0b0000100000000000) { // sign bit is set
        TAMB *= -1;
    }

    double ambient = TAMB * 0.0625;

    if(unlikely(SUCCESS != tw->write(args->args[3], (uint8_t*)&ambient, sizeof(double)))) {
        return FAILURE;
    }

    return SUCCESS;

    // Vout = (41.276 uV / C) * (TR - TAMB)
    // Vout = (41.276 uV / C) * (degree_ratio)(TR_reading - TAMB_reading)

    // degree ratio is how many degrees C per magnitude of reading (assuming linear scale)
    // comes from manual page 10, 0b01100100000000 equates to 1600 C
    // this yields a ratio of 4 C per magnitude

    // Vout = (4 * 41.276 uV / C) * (degree_ratio)(TR_reading - TAMB_reading)

    // vout is the voltage output of the thermocouple in mV
    // derived from the termperatures reported by the MAX31855K which assumed a linear scale
    // we can use the temperature readings to calculate vout and find the termperature using a non-linear method

    // the multiplication of constants should get optimized out
    double vout = (4 * 0.041276) * (TR - TAMB);

    // find which range vout is in
    if(vout < -5.891) {
        // out of range for NIST polynomials
        return FAILURE;
    }

    double t = 0.0;
    double E = 1.0;

    if(vout < 0) {
        // use polynomial 1
        for(size_t i = 0; i < K_INVERSE_R0_NUM; i++) {
            t += (dr0[i] * E);
            E *= vout;
        }
    } else if(vout < 20.664) {
        // use polynomial 2
        for(size_t i = 0; i < K_INVERSE_R1_NUM; i++) {
            t += (dr1[i] * E);
            E *= vout;
        }
    } else if(vout < 54.886) {
        // use polynnomial 3
        for(size_t i = 0; i < K_INVERSE_R2_NUM; i++) {
            t += (dr2[i] * E);
            E *= vout;
        }
    } else {
        // too high
        // this is readings over 1372 C, which we can't read
        return FAILURE;
    }

    // t is relative temp (calculated)

    return SUCCESS;
}
