/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2016 ObdDiag.Net. All rights reserved.
 *
 */

#include <memory>
#include <cstdio>
#include <adaptertypes.h>
#include <Timer.h>
#include <candriver.h>
#include <led.h>
#include "canmsgbuffer.h"
#include <algorithms.h>
#include "obdprofile.h"
#include "j1979.h"
#include "isocan.h"
#include "canhistory.h"

using namespace std;
using namespace util;

const int CAN_FRAME_LEN = 8;

IsoCanAdapter::IsoCanAdapter()
{
    extended_   = false;
    driver_     = CanDriver::instance();
    history_    = new CanHistory();
    sts_        = REPLY_NO_DATA;
    canExtAddr_ = false;
    formatter_  = new CanReplyFormatter();
}

/**
 * Send buffer to ECU using CAN
 * @param[in] data The message data bytes
 * @param[in] len The message length
 * @return true if OK, false if data issues
 */
bool IsoCanAdapter::sendToEcu(const uint8_t* buff, int length)
{
    uint8_t dlc = 0;
    uint8_t data[CAN_FRAME_LEN] = {0};
    
    if (length > 0x0FFF)
        return false; // The max CAN length

    bool caf1Option = config_->getBoolProperty(PAR_CAN_CAF);
    
    // Extended address byte
    const ByteArray* canExt = config_->getBytesProperty(PAR_CAN_EXT);
    int totalLen = canExt->length ? (length + 2) : (length + 1); // + cea byte, + len byte
    if (!caf1Option) {
        totalLen--; // not counting len byte
    }
    canExtAddr_ = canExt->length; // set class instance member
    
    if (totalLen <= CAN_FRAME_LEN) { 
        int i = 0;
        if (canExt->length) { // CAN extended addressing
            data[i++] = canExt->data[0];
        }

        // OBD type format with dlc=8, and first byte = length
        //
        dlc = (getProtocol() != PROT_ISO15765_USR_B) ? CAN_FRAME_LEN : totalLen;
        
        // If CAF1
        if (caf1Option) {
            data[i++] = length;
        }
        
        memcpy(data + i, buff, length);
        return sendFrameToEcu(data, totalLen, dlc);
    }
    else {
        return sendToEcuMF(buff, length);
    }
}

/**
 * Send a single CAN frame to ECU
 * @param[in] data The message data bytes
 * @param[in] length The message length
 * @param[in] dlc The message DLC
 * @return true if OK, false if data issues
 */
bool IsoCanAdapter::sendFrameToEcu(const uint8_t* data, uint8_t length, uint8_t dlc)
{
    CanMsgBuffer msgBuffer(getID(), extended_, dlc, 0);
    memcpy(msgBuffer.data, data, length);
    
    // Message log
    history_->add2Buffer(&msgBuffer, true, 0);

    if (!driver_->send(&msgBuffer)) { 
        return false; // REPLY_DATA_ERROR
    }
    return true;
}

/**
 * Send CAN multi frames CAN to ECU
 * @param[in] data The message data bytes
 * @param[in] length The message length
 * @return true if OK, false if data issues
 */
bool IsoCanAdapter::sendToEcuMF(const uint8_t* buff, int length)
{
    int idx = 0;
    uint8_t dlc = 0;
    uint8_t data[CAN_FRAME_LEN] = {0};
    const ByteArray* canExt = config_->getBytesProperty(PAR_CAN_EXT);
    
    // First frame
    //
    dlc = 8; // Its a first frame anyway
    if (canExtAddr_)
        data[idx++] = canExt->data[0];
    data[idx] = 0x10; // First frame indicator
    data[idx++] |= (length & 0xF00) >> 8; // Maximum 4095 bytes, 1.5 byte len
    data[idx++] = length & 0xFF;  
    int numBytesSent = canExtAddr_ ? 5 : 6;
    memcpy(data + idx, buff, numBytesSent);
    if (!sendFrameToEcu(data, dlc, dlc))
        return false;
    
    // Wait for control frame
    //
    uint8_t fs, bs, stmin;
    if (!receiveControlFrame(fs, bs, stmin))
        return false; // error getting control frame
    if (fs != 0) 
        return false; // the destination is not ready
    uint32_t frameDelay = stmin;
    
    // The rest of frames
    //
    const int frameDataLen = canExtAddr_ ? 6 : 7;
    int restFrameNum = (length - numBytesSent) / frameDataLen;
    if ((length - numBytesSent) % frameDataLen)
        restFrameNum++;
    
    uint8_t sn = 0x21; // The SN start with the one
    for (int i = 0; i < restFrameNum; i++) {
        int numBytesLeft = length - numBytesSent;
        uint8_t numToSend = (numBytesLeft > frameDataLen) ? frameDataLen : numBytesLeft;
        
        idx = 0;
        if (canExtAddr_)
            data[idx++] = canExt->data[0];
        data[idx++] = sn++;
        dlc = idx + numToSend;
        memcpy(data + idx, buff + numBytesSent, numToSend);
        // In some cases dlc is always 8 ?
        if (!sendFrameToEcu(data, dlc, dlc))
            return false;
        
        numBytesSent += numToSend;
        
        // SN rollover
        if (sn > 0x2F) 
            sn = 0x20;
        
        // frame delay
        if (frameDelay > 0)
            Delay1ms(frameDelay);
    }
    
    return true;
}

/**
 * Timing Exceptions handler, requestCorrectlyReceived-ResponsePending
 * @param[in] msg CanMsgbuffer instance pointer
 * @return true if timeout exception, false otherwise
 */
bool IsoCanAdapter::checkResponsePending(const CanMsgBuffer* msg)
{
    int offst = canExtAddr_ ? 1 : 0;
    
    // Looking for "requestCorrectlyReceived-ResponsePending", 7F.XX.78
    if (msg->data[1 + offst] == 0x7F && msg->data[3 + offst] == 0x78) {
        return true;
    }
    return false;
}

/**
 * Receives a sequence of bytes from the CAN bus
 * @param[in] sendReply send reply to user flag
 * @return true if message received, false otherwise
 */
bool IsoCanAdapter::receiveFromEcu(bool sendReply)
{
    const int MAX_PEND_RESP_NUM = 100;
    int pendRespCounter = 0;
    const int p2Timeout = getP2MaxTimeout();
    CanMsgBuffer msgBuffer;
    bool msgReceived = false;
    canExtAddr_ = config_->getBytesProperty(PAR_CAN_EXT)->length; // set class instance member
    int frameNum = 0;
    
    Timer* timer = Timer::instance(0);
    timer->start(p2Timeout);

    do {
        if (!driver_->isReady())
            continue;
        driver_->read(&msgBuffer);
        
        // Message log
        history_->add2Buffer(&msgBuffer, false, msgBuffer.msgnum);
        
        if (!checkResponsePending(&msgBuffer) || pendRespCounter > MAX_PEND_RESP_NUM) {
            // Reload the timer, regular P2 timeout
            timer->start(p2Timeout);
        }
        else {
            // Reload the timer, P2* timeout
            timer->start(P2_MAX_TIMEOUT_S);
            pendRespCounter++;
        }
        
        // Check the CAN receiver address
        /*
        if (canExt) {
            if (msgBuffer.data[0] != testerAddress)
                continue;
        }*/

        msgReceived = true;
        if (!sendReply)
            continue;
        
        // CAN extextended address
        uint8_t keyByte = canExtAddr_ ? msgBuffer.data[1] : msgBuffer.data[0];
        switch ((keyByte & 0xF0) >> 4) {
            case CANSingleFrame:
                formatter_->reply(&msgBuffer); //processFrame(&msgBuffer);
                break;
            case CANFirstFrame:
                processFlowFrame(&msgBuffer);
                formatter_->replyFirstFrame(&msgBuffer);//processFirstFrame(&msgBuffer);
                break;
            case CANConsecutiveFrame:
                formatter_->replyNextFrame(&msgBuffer, ++frameNum);//processNextFrame(&msgBuffer, ++frameNum);
                break;
            default:
                formatter_->reply(&msgBuffer); //processFrame(&msgBuffer); // oops
        }
    } while (!timer->isExpired());

    return msgReceived;
}

bool IsoCanAdapter::receiveControlFrame(uint8_t& fs, uint8_t& bs, uint8_t& stmin)
{
    const int p2Timeout = getP2MaxTimeout();
    CanMsgBuffer msgBuffer;
    
    Timer* timer = Timer::instance(0);
    timer->start(p2Timeout);

    do {
        if (!driver_->isReady())
            continue;
        driver_->read(&msgBuffer);
        
        // Message log
        history_->add2Buffer(&msgBuffer, false, msgBuffer.msgnum);

        if (!canExtAddr_ && (msgBuffer.data[0] & 0xF0) == 0x30) {
            fs = msgBuffer.data[0] & 0x0F;
            bs = msgBuffer.data[1];
            stmin = msgBuffer.data[2];
            return true;
        }
        else if (canExtAddr_ && (msgBuffer.data[1] & 0xF0) == 0x30) {
            fs = msgBuffer.data[1] & 0x0F;
            bs = msgBuffer.data[2];
            stmin = msgBuffer.data[3];
            return true;
        }
    } while (!timer->isExpired());

    return false;
}

int IsoCanAdapter::getP2MaxTimeout() const
{
    int p2Timeout = config_->getIntProperty(PAR_TIMEOUT);
    int p2Mult = config_->getIntProperty(PAR_CAN_TIMEOUT_MULT);
    return p2Timeout ? (p2Timeout * 4 * p2Mult) : P2_MAX_TIMEOUT;
}

/**
 * Global entry ECU send/receive function
 * @param[in] data The message data bytes
 * @param[in] len The message length
 * @return The completion status code
 */
int IsoCanAdapter::onRequest(const uint8_t* data, int len)
{
    if (!sendToEcu(data, len))
        return REPLY_DATA_ERROR;
    return receiveFromEcu(true) ? REPLY_NONE : REPLY_NO_DATA;
}

/**
 * Will try to send PID0 to query the CAN protocol
 * @param[in] sendReply Reply flag
 * @return Protocol value if ECU is supporting CAN protocol, 0 otherwise
 */
int IsoCanAdapter::onTryConnectEcu(bool sendReply)
{
    CanMsgBuffer msgBuffer(getID(), extended_, 8, 0x02, 0x01, 0x00);
    sts_ = REPLY_OK;
    sampleSent_ = false;
    open();

    if (!config_->getBoolProperty(PAR_BYPASS_INIT)) {
        if (driver_->send(&msgBuffer)) { 
            if (receiveFromEcu(sendReply)) {
                connected_ = true;
                sampleSent_= sendReply;
                return extended_ ? PROT_ISO15765_2950 : PROT_ISO15765_1150;
            }
        }
        close(); // Close only if not succeeded
        sts_ = REPLY_NO_DATA;
        return 0;
    }
    else {
        connected_ = true;
        return extended_ ? PROT_ISO15765_2950 : PROT_ISO15765_1150;
    }
}

/**
 * Print the messages buffer
 */
void IsoCanAdapter::dumpBuffer()
{
    history_->dumpCurrentBuffer();
}

/**
 * Test wiring connectivity for CAN
 */
void IsoCanAdapter::wiringCheck()
{
    bool failed = false;
    driver_->setBitBang(true);

    driver_->setBit(0);
    Delay1us(100);
    if (driver_->getBit() != 0) {
        AdptSendReply("CAN wiring failed [0->1]");
        failed = true;
    }        

    driver_->setBit(1);
    Delay1us(100);
    if (driver_->getBit() != 1) {
        AdptSendReply("CAN wiring failed [1->0]");
        failed = true;
    }    
    
    if (!failed) {
        AdptSendReply("CAN wiring is OK");
    }

    driver_->setBitBang(false);
}

/**
 * IsoCan11Adapter class members
 */
void IsoCan11Adapter::open()
{
    setFilterAndMask();
    
    //Start using LED timer
    AdptLED::instance()->startTimer();
}

int IsoCan11Adapter::onConnectEcu()
{
    connected_ = true;
    return getProtocol();
}

uint32_t IsoCan11Adapter::getID() const
{ 
    IntAggregate id;

    const ByteArray* hdr = config_->getBytesProperty(PAR_HEADER_BYTES);
    if (hdr->length) {
        id.lvalue = hdr->asCanId() & 0x7FF;
    }
    else {
        id.lvalue = 0x7DF;
    }
    return id.lvalue;
}

void IsoCan11Adapter::setFilterAndMask()
{
    // Mask 11 bit
    IntAggregate mask;
    const ByteArray* maskBytes = config_->getBytesProperty(PAR_CAN_MASK);
    if (maskBytes->length) {
        mask.lvalue = maskBytes->asCanId() & 0x7FF;
    }
    else { // Default mask for 11 bit CAN
        mask.lvalue = 0x7F8;
    }
    
    // Filter 11 bit
    IntAggregate filter;
    const ByteArray* filterBytes = config_->getBytesProperty(PAR_CAN_FILTER);
    if (filterBytes->length) {
        filter.lvalue = filterBytes->asCanId() & 0x7FF;
    }
    else { // Default filter for 11 bit CAN
        filter.lvalue = 0x7E8;
    }
    
    // Set mask and filter 11 bit
    driver_->setFilterAndMask(filter.lvalue, mask.lvalue, false);
}

void IsoCan11Adapter::processFlowFrame(const CanMsgBuffer* msg)
{
    if (!config_->getBoolProperty(PAR_CAN_FLOW_CONTROL))
        return; // ATCFC0
    
    int flowMode = config_->getIntProperty(PAR_CAN_FLOW_CTRL_MD);
    const ByteArray* hdr = config_->getBytesProperty(PAR_CAN_FLOW_CTRL_HDR);
    const ByteArray* bytes = config_->getBytesProperty(PAR_CAN_FLOW_CTRL_DAT);
    
    CanMsgBuffer ctrlData(0x7E0, false, 8, 0x30, 0x00, 0x00);
    ctrlData.id |= (msg->id & 0x07); //Figure out the return address
    
    if(flowMode == 1 && hdr != nullptr) {
        ctrlData.id = hdr->asCanId() & 0x7FF;
    }
    if(flowMode > 0 && bytes != nullptr) {
        memset(ctrlData.data, CanMsgBuffer::DefaultByte, sizeof(ctrlData.data));
        memcpy(ctrlData.data, bytes->data, bytes->length);
    }
    driver_->send(&ctrlData);
    
    // Message log
    history_->add2Buffer(&ctrlData, true, 0);
}

int IsoCan11Adapter::getProtocol() const
{ 
    return config_->getIntProperty(PAR_PROTOCOL);
}

void IsoCan11Adapter::getDescription()
{
    const char* desc = nullptr;
    bool useAutoSP = config_->getBoolProperty(PAR_USE_AUTO_SP);
    int protocol = getProtocol();
    
    if (protocol == PROT_ISO15765_USR_B) {
        desc = "USER1 (CAN 11/500)";
    }
    else {
        desc = useAutoSP ? "AUTO, ISO 15765-4 (CAN 11/500)" : "ISO 15765-4 (CAN 11/500)";
    }
    AdptSendReply(desc);
}

void IsoCan11Adapter::getDescriptionNum()
{
    const char* desc = nullptr;
    bool useAutoSP = config_->getBoolProperty(PAR_USE_AUTO_SP);
    int protocol = getProtocol();
    
    if (protocol == PROT_ISO15765_USR_B) {
        desc = "B";
    }
    else {
        desc = useAutoSP ? "A6" : "6";
    }
    AdptSendReply(desc); 
}

void IsoCan11Adapter::setReceiveAddress(const util::string& par)
{
}

/**
 * IsoCan29Adapter class members
 */
void IsoCan29Adapter::open()
{
    setFilterAndMask();
    
    // Start using LED timer
    AdptLED::instance()->startTimer();
}

int IsoCan29Adapter::onConnectEcu()
{
    connected_ = true;
    return getProtocol();
}

uint32_t IsoCan29Adapter::getID() const
{ 
    IntAggregate id;
    uint8_t canPriority = 0;
    
    // Get the highest 29bit header byte
    const ByteArray* prioBits = config_->getBytesProperty(PAR_CAN_PRIORITY_BITS);
    if (prioBits->length) {
        canPriority = prioBits->data[0] & 0x1F;
    }
    else {
        canPriority = 0x18; // Default for OBD  
    }
    
    const ByteArray* hdr = config_->getBytesProperty(PAR_HEADER_BYTES);
    if (hdr->length) {
        id.lvalue = hdr->asCanId();
        id.bvalue[3] = canPriority;
    }
    else {
        id.lvalue = 0x18DB33F1;
    }
    return id.lvalue;
}

void IsoCan29Adapter::setFilterAndMask() 
{
    // Mask 29 bit
    IntAggregate mask;
    const ByteArray* maskBytes = config_->getBytesProperty(PAR_CAN_MASK);
    if (maskBytes->length) {
        mask.lvalue = maskBytes->asCanId() & 0x1FFFFFFF;
    }
    else { // Default mask for 29 bit CAN
        mask.lvalue = 0x1FFFFF00;
    }
    
    // Filter 29 bit
    IntAggregate filter;
    const ByteArray* filterBytes = config_->getBytesProperty(PAR_CAN_FILTER);
    if (filterBytes->length) {
        filter.lvalue = filterBytes->asCanId() & 0x1FFFFFFF;
    }
    else { // Default for 29 bit CAN
        filter.lvalue = 0x18DAF100;
    }
    
    // Set mask and filter 29 bit
    driver_->setFilterAndMask(filter.lvalue, mask.lvalue, true);
}

void IsoCan29Adapter::processFlowFrame(const CanMsgBuffer* msg)
{
    CanMsgBuffer ctrlData(0x18DA00F1, true, 8, 0x30, 0x0, 0x00);
    ctrlData.id |= (msg->id & 0xFF) << 8;
    driver_->send(&ctrlData);
    
    // Message log
    history_->add2Buffer(&ctrlData, true, 0);
}

void IsoCan29Adapter::getDescription()
{
    bool useAutoSP = config_->getBoolProperty(PAR_USE_AUTO_SP);
    AdptSendReply(useAutoSP ? "AUTO, ISO 15765-4 (CAN 29/500)" : "ISO 15765-4 (CAN 29/500)");
}

void IsoCan29Adapter::getDescriptionNum()
{
    bool useAutoSP = config_->getBoolProperty(PAR_USE_AUTO_SP);
    AdptSendReply(useAutoSP ? "A7" : "7"); 
}

void IsoCan29Adapter::setReceiveAddress(const util::string& par)
{
}
