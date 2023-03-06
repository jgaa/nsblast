#include "SlaveMgr.h"
#include "Slave.h"

#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;


namespace nsblast::lib {

Slave::Slave(SlaveMgr &mgr)
    : mgr_{mgr}
{

}

void Slave::start()
{
    // Set a timer to a random time withing the refresh-window

    // When the timer is triggered, do a client transfer

        // Delete the Zone in Entry contxt
        // Add all the RR's until the final soa
        // If the transfer is successful and valid, cimmit the transaction.
        // Set a new timer, this time for the refresh-value
        // repeat
}


} // ns
