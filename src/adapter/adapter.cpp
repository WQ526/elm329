/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2017 ObdDiag.Net. All rights reserved.
 *
 */

#include <lstring.h>
#include <cctype>
#include <Timer.h>
#include <GPIODrv.h>
#include <CmdUart.h>
#include <CanDriver.h>
#include <AdcDriver.h>
#include <led.h>
#include <adaptertypes.h>
#include <datacollector.h>

using namespace std;
using namespace util;

static CmdUart* glblUart;
static DataCollector* collector = DataCollector::instance();

/**
 * Enable the clocks and peripherals, initialize the drivers
 */
static void SetAllRegisters()
{
    Timer::configure();
    GPIOConfigure(0);    // GPIOA clock
    GPIOConfigure(1);    // GPIOB clock
    CmdUart::configure();
    CanDriver::configure();
    AdptLED::configure();
    AdcDriver::configure();
}

/**
 * Outer interface UART receive callback
 * @param[in] ch Character received from UART
 */
static bool UserUartRcvHandler(uint8_t ch)
{
    bool ready = false;
    
    if (AdapterConfig::instance()->getBoolProperty(PAR_ECHO) && ch != '\n') {
        glblUart->send(ch);
        if (ch == '\r' && AdapterConfig::instance()->getBoolProperty(PAR_LINEFEED)) {
            glblUart->send('\n');
        }
    }
    
    if (ch == '\r') { // Got cmd terminator
        ready = true;
    }
    else if (isprint(ch)) { // this will skip '\n' as well
        collector->putChar(ch);
    }
    
    return ready;
}

/**
 * Send string to UART
 * @param[in] str String to send
 */
void AdptSendString(const util::string& str)
{
    glblUart->send(str);
}

/**
 * Adapter main loop
 */
static void AdapterRun() 
{
    glblUart = CmdUart::instance();
    glblUart->init(UART_SPEED);
    glblUart->handler(UserUartRcvHandler);
    AdptPowerModeConfigure();
    AdptDispatcherInit();

    for(;;) {    
        if (glblUart->ready()) {
            glblUart->ready(false);
            AdptOnCmd(collector);
            collector->reset();
        }
        __WFI(); // goto sleep
    }

}

int main(void)
{
    SystemCoreClockUpdate();
    
    SetAllRegisters();
    AdapterRun();
}

