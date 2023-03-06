#pragma once

#include <memory>
#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "proto/nsblast.pb.h"


namespace nsblast::lib {

class SlaveMgr;

class Slave : public std::enable_shared_from_this<Slave> {
public:
    Slave(SlaveMgr& mgr);

    void start();

private:
    SlaveMgr& mgr_;
};

} // ns
