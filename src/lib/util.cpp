#include "nsblast/util.h"

namespace nsblast::lib {

boost::uuids::uuid newUuid()
{
    static boost::uuids::random_generator uuid_gen_;
    return uuid_gen_();
}



}
