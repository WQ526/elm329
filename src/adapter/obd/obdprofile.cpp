/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2016 ObdDiag.Net. All rights reserved.
 *
 */

#include <cstdio>
#include "obdprofile.h"
#include <datacollector.h>

using namespace util;

//
// Reply error string constants
//
static const char ErrMessage [] = "?";
static const char Err1Message[] = "DATA ERROR";        // Bad ECU response
static const char Err2Message[] = "NO DATA";           // No response
static const char Err3Message[] = "ERROR";             // Got response but invalid
static const char Err4Message[] = "UNABLE TO CONNECT"; // All attempts to connect failed (for auto connect)
static const char Err5Message[] = "FB ERROR";          // Wiring
static const char Err6Message[] = "BUS BUSY";          // Bus collision or busy
static const char Err7Message[] = "BUS ERROR";         // Bus error
static const char Err8Message[] = "DATA ERROR>";       // Checksum
static const char Err0Message[] = "Program Error";     // Wrong coding?


/**
 * Instance accessor
 * @return The OBDProfile instace pointer
 **/
OBDProfile* OBDProfile::instance()
{
    static OBDProfile obdProfile;
    return &obdProfile;
}

/**
 * Construct OBDProfile object
 */
OBDProfile::OBDProfile()
{
    adapter_ = ProtocolAdapter::getAdapter(ADPTR_AUTO);
}

/**
 * Proxies....
 */
void OBDProfile::getProtocolDescription() const
{
    adapter_->getDescription();
}

void OBDProfile::getProtocolDescriptionNum() const
{
    adapter_->getDescriptionNum();
}

void OBDProfile::dumpBuffer()
{
    adapter_->dumpBuffer();
}

/**
 * Set the protocol number
 * @param[in] num The protocol number
 * @param[in] refreshConnection The flag
 * @return The completion status
 */
int OBDProfile::setProtocol(int num, bool refreshConnection)
{
    ProtocolAdapter* pvadapter = adapter_;
    switch (num) {
        case PROT_AUTO:
            adapter_ = ProtocolAdapter::getAdapter(ADPTR_AUTO);
            break;
        case PROT_ISO15765_1150:
        case PROT_ISO15765_USR_B:
            adapter_ = ProtocolAdapter::getAdapter(ADPTR_CAN);
            break;
        case PROT_ISO15765_2950:
            adapter_ = ProtocolAdapter::getAdapter(ADPTR_CAN_EXT);
            break;
        default:
            return REPLY_CMD_WRONG;
    }
    // Do this if only "ATSP" executed
    if (refreshConnection) {
        if (pvadapter != adapter_) { 
            pvadapter->close();
            adapter_->open();
        }
    }
    return REPLY_OK;
}

/**
 * The entry for ECU send/receive function
 * @param[in] collector The command
 * @return The status code
 */
void OBDProfile::onRequest(const DataCollector* collector)
{
    char prefix[12];
    
    int result = onRequestImpl(collector);
    switch(result) {
        case REPLY_CMD_WRONG:
            AdptSendReply(ErrMessage);
            break;
        case REPLY_DATA_ERROR:
            AdptSendReply(Err1Message);
            break;
        case REPLY_NO_DATA:
            AdptSendReply(Err2Message);
            break;
        case REPLY_ERROR:
            AdptSendReply(Err3Message);
            break;
        case REPLY_UNBL_2_CNNCT:
            AdptSendReply(Err4Message);
            break;
        case REPLY_BUS_BUSY:
            AdptSendReply(Err6Message);
            break;        
        case REPLY_BUS_ERROR:
            AdptSendReply(Err7Message);
            break;
        case REPLY_CHKS_ERROR:
            AdptSendReply(Err8Message);
            break;
        case REPLY_WIRING_ERROR:
            AdptSendReply(Err5Message);
            break;        
        case REPLY_NONE:
        case 0:
            break;
        case REPLY_OK:
            break;
        default:
            sprintf(prefix, "%X ", result);
            AdptSendString(prefix);
            AdptSendReply(Err0Message);
            break;
    }
}    

/**
 * The actual implementation of request handler
 * @param[in] collector The command
 * @return The status code
 */
int OBDProfile::onRequestImpl(const DataCollector* collector)
{
    const char* OBD_TEST_SEQ = "0100";

    // Valid request length?
    if (!sendLengthCheck(collector->getLength())) {
        return REPLY_DATA_ERROR;
    }

    // The regular flow stops here
    if (adapter_->isConnected()) {
        return adapter_->onRequest(collector->getData(), collector->getLength()); //1
    } 

    // Convoluted logic
    //
    bool sendReply = (collector->getString() == OBD_TEST_SEQ);
    int protocol = 0;
    int sts = REPLY_NO_DATA;
    
    ProtocolAdapter* autoAdapter = ProtocolAdapter::getAdapter(ADPTR_AUTO);
    autoAdapter->sampleSent(false);
    
    if (adapter_ == autoAdapter) {
        protocol = autoAdapter->onTryConnectEcu(sendReply);
        // onConnectEcu() not returning status, just query it
        sts = autoAdapter->getStatus();
    }
    else {
        bool useAutoSP = AdapterConfig::instance()->getBoolProperty(PAR_USE_AUTO_SP);
        if (useAutoSP) {
            protocol = adapter_->onTryConnectEcu(sendReply); // returns 0 if nost succeeded
        }
        else {
            protocol = adapter_->onConnectEcu(); // always return protocol number, not 0
        }
        
        sts = adapter_->getStatus();
        if (protocol == 0 && useAutoSP) { //&& sts == REPLY_NO_DATA
            protocol = autoAdapter->onTryConnectEcu(sendReply);
            // and get status one more time
            sts = autoAdapter->getStatus(); //4
        }
    }
    
    // ISO9141/14230 do not send any commands to ECU
    // but CAN do if 
    if (protocol) {
        setProtocol(protocol, false);
        if (!autoAdapter->isSampleSent()) {
            sts = adapter_->onRequest(collector->getData(), collector->getLength()); //5
        }
        else {
            sts = REPLY_NONE; //the command sent already as part of autoconnect
        }
    }
    return sts;
}

/**
 * Check the maximum length for OBD request
 * @param[in] len The request length
 * @return true if set, false otherwise
 */
bool OBDProfile::sendLengthCheck(int len)
{
    int maxLen = OBD_IN_MSG_DLEN; // set to 255

    // For KWP use maxlen=8
    //if (adapter_ ==  ProtocolAdapter::getAdapter(ADPTR_ISO)) {
    //    maxLen++;
    //}
    if ((len == 0) || len > maxLen) { // 255 bytes
        return false;
    }
    return true;
}

int OBDProfile::getProtocol() const
{
    return adapter_->getProtocol();
}

void OBDProfile::closeProtocol()
{
    adapter_->close();
}

/**
 * ISO KW1,KW2 display, applies only to 9141/14230 adapter
 */
int OBDProfile::kwDisplay()
{
    return REPLY_OK;
}

/**
 * ISO 9141/14230 hearbeat, implemented only in ISO serial adapter
 */
void OBDProfile::sendHeartBeat()
{
    adapter_->sendHeartBeat();
}

/**
 * Test wiring connectivity for all protocols
 */
void OBDProfile::wiringCheck()
{
    ProtocolAdapter::getAdapter(ADPTR_CAN)->wiringCheck();
}

void OBDProfile::setFilterAndMask()
{
    adapter_->setFilterAndMask();
}
