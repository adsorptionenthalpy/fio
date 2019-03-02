/** chain_control definitions file
 *  Description: Sets up the supported chain data structure
 *  @author Casey Gardiner
 *  @file fio_name_validator.hpp
 *  @copyright Dapix
 *
 *  Changes:
 */

#include <string>
#include <fc/io/json.hpp>
#include <boost/filesystem.hpp>

namespace fioio {

    using namespace std;
    namespace bs = boost::filesystem;

    struct clentry {
        uint32_t index;
        fc::string   chain;
    };
    struct clist {
        vector<clentry> chains;
    };

    struct chainControl {
        clist chainList;
        bs::path JSONFILE;

        chainControl( )
                :JSONFILE ("config/bip44chains.json")
        {

            bs::path cp = bs::current_path() / "config" / "bip44chains.json";

            try {
                fc::json::from_file(cp).as<clist>(chainList);
            } catch (const fc::exception ex) {
                elog ("from_file threw ${ex}",("ex",ex));
            } catch (...) {
                elog ("failed to read ${f}",("f",cp.string()));
            }
            size_t csize = chainList.chains.size();
            size_t ref = csize > 100 ? 100 : csize - 1;
            ilog("chainList: size = ${s} chain[0] =  ${t} chain[${r}] = ${h}",
                 ("s",csize)("t", chainList.chains[0].chain)("r",ref)("h",chainList.chains[ref].chain));
        }

        inline string getChainFromIndex(int index) {
            string chainName = chainList.chains[index].chain;

            return chainName;
        }

        //inline int getIndexFromChain( string chainname ){
        //    vector<string>::iterator it = find(chainList.chains.begin(), chainList.chains.end(), chainname);
        //    int index = distance(chainList.chains.begin(), it);
        //    int result = -1;

        //    if(it != chainList.chains.end()) {
        //        result = distance(chainList.chains.begin(), it);
        //    } else {
        //        //ASSERT
        //    }

        //    return result;
        //}
    };
}

FC_REFLECT(fioio::clentry, (index)(chain));
FC_REFLECT(fioio::clist, (chains));
