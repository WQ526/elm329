/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2016 ObdDiag.Net. All rights reserved.
 *
 */

#include "padapter.h"
#include "autoadapter.h"
#include "isocan.h"

using namespace util;

/**
 * Constructs ProtocolAdater
 */
ProtocolAdapter::ProtocolAdapter()
{ 
    config_ = AdapterConfig::instance();
    close();
}

void ProtocolAdapter::close()
{
    connected_ = false;
    sts_ = REPLY_NO_DATA;
    sampleSent_ = false;
}

/**
 * ProtocolAdapter object factory
 * @param[in] adapterType The adapter type number
 * @return The ProtocolAdapter pointer
 **/
ProtocolAdapter* ProtocolAdapter::getAdapter(int adapterType)
{
    static AutoAdapter autoAdapter;
    static IsoCan11Adapter canAdapter;
    static IsoCan29Adapter canExtAdapter;
    
    switch (adapterType) {
        case ADPTR_AUTO:
            return &autoAdapter;
        case ADPTR_CAN:
            return &canAdapter;
        case ADPTR_CAN_EXT:
            return &canExtAdapter;
        default:
            return nullptr;
    }
}
