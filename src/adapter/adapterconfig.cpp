/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2016 ObdDiag.Net. All rights reserved.
 *
 */

#include <cstring>
#include <adaptertypes.h>
#include <cortexm.h>

using namespace std;
const uint64_t onebit = 1;

//
// Configuration settings, storing/retrieving properties
//

AdapterConfig::AdapterConfig() : values_(0)
{
    memset(intProps_, 0, sizeof(intProps_));
}
    
void AdapterConfig::setBoolProperty(int id, bool val)
{
    if (id >= BYTE_PROPS_START && id < BYTE_PROPS_END) {
        if (id > 64) 
            return;
        values_ = val ? (values_ | (onebit << id)) : (values_ & ~(onebit << id));
    }
    else {
        __BKPT(0);
    }
}

bool AdapterConfig::getBoolProperty(int id) const
{
    if (id >= BYTE_PROPS_START && id < BYTE_PROPS_END) {
        if (id > 64) 
            return false;
        return values_ & (onebit << id);
    }
    else {
        __BKPT(0);
    }
    return false;
}

void AdapterConfig::setIntProperty(int id, uint32_t val) 
{
    if (id >= INT_PROPS_START && id < INT_PROPS_END) {
        int idx = id - INT_PROPS_START;
        intProps_[idx] = val;
    }
    else {
        __BKPT(0);
    }
}

uint32_t AdapterConfig::getIntProperty(int id) const
{
    if (id >= INT_PROPS_START && id < INT_PROPS_END) {
        int idx = id - INT_PROPS_START;
        return intProps_[idx];
    }
    else {
        __BKPT(0);
    }
    return 0;
}

void AdapterConfig::setBytesProperty(int id, const ByteArray* bytes)
{
    if (id >= BYTES_PROPS_START && id < BYTES_PROPS_END) {
        int idx = id - BYTES_PROPS_START;
        bytesProps_[idx] = *bytes;
    }
    else {
        __BKPT(0);
    }
}

const ByteArray* AdapterConfig::getBytesProperty(int id) const
{
    if (id >= BYTES_PROPS_START && id < BYTES_PROPS_END) {
        int idx = id - BYTES_PROPS_START;
        return &bytesProps_[idx];
    }
    else {
        __BKPT(0);
    }
    return nullptr;
}

AdapterConfig* AdapterConfig::instance()
{
    static AdapterConfig instance;
    return &instance;
}

void AdapterConfig::clear()
{
    for (uint8_t& prop : byteProps_) {
        prop = false;
    }
    for (uint32_t& prop : intProps_) {
        prop = 0;
    }
    for (ByteArray& ba : bytesProps_) {
        ba.clear();
    }
}
