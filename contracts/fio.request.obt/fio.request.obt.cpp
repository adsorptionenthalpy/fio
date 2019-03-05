/** Fio Request Obt implementation file
 *  Description: FioRequestObt smart contract supports funds request and other block chain transaction recording.
 *  @author Ed Rotthoff
 *  @file fio.request.obt.cpp
 *  @copyright Dapix
 *
 *  Changes:
 */

#include <eosiolib/asset.hpp>
#include "fio.request.obt.hpp"
#include "../fio.name/fio.name.hpp"
#include <fio.common/fio.common.hpp>
#include <fio.common/json.hpp>
#include <eosio/chain/fioio/fioerror.hpp>
#include <eosio/chain/fioio/fio_common_validator.hpp>
#include <climits>


namespace fioio {


    class FioRequestObt : public contract {

    private:
        fiorequest_contexts_table fiorequestContextsTable;
        fiorequest_status_table fiorequestStatusTable;
    public:
        explicit FioRequestObt(account_name self)
                : contract(self), fiorequestContextsTable(self,self), fiorequestStatusTable(self,self) {}


        //the json.hpp library wasnt working as expected from the smart contract
        //so i wrote some quick parsing to parse the necessary values out of the
        //input json. we can look for a better way to do this later.
        std::vector<std::string> split (const std::string &s, char delim) {
            std::vector<std::string> result;
            std::stringstream ss (s);
            std::string item;

            while (getline (ss, item, delim)) {
                result.push_back (item);
            }

            return result;
        }

        //this will return the value of a string for the chars after the first colon in the string,
        //the result will have leading and trailing whitespace and leading and trailing " chars stripped from the string.
        std::string getValueAfterColon(const std::string &s) {

            string result  = s.substr(s.find(":") + 1);
            if (!result.empty())
            {
                //strip leading and trailing whitespace and leading and trailing "
                result.erase(result.begin(), std::find_if(result.begin(), result.end(), std::bind1st(std::not_equal_to<char>(), ' ')));
                result.erase(std::find_if(result.rbegin(), result.rend(), std::bind1st(std::not_equal_to<char>(), ' ')).base(), result.end());
                result.erase(result.begin(), std::find_if(result.begin(), result.end(), std::bind1st(std::not_equal_to<char>(), '"')));
                result.erase(std::find_if(result.rbegin(), result.rend(), std::bind1st(std::not_equal_to<char>(), '"')).base(), result.end());
            }

            return result;

        }


        //this method will string the leading and trailing curly braces and spaces.
        std::string stripLeadingAndTrailingBraces(const std::string &s){

            std::string result = s;
            result.erase(result.begin(), std::find_if(result.begin(), result.end(), std::bind1st(std::not_equal_to<char>(), '{')));
            result.erase(std::find_if(result.rbegin(), result.rend(), std::bind1st(std::not_equal_to<char>(), ' ')).base(), result.end());
            result.erase(result.begin(), std::find_if(result.begin(), result.end(), std::bind1st(std::not_equal_to<char>(), '{')));
            result.erase(std::find_if(result.rbegin(), result.rend(), std::bind1st(std::not_equal_to<char>(), '}')).base(), result.end());

            return result;
        }


        /***
         * this action will record a send using Obt. the input json will be verified, if verification fails an exception will be thrown.
         * if verification succeeds then status tables will be updated...
         */
        // @abi action
        void recordsend(const string &recordsend, const string &actor) {
            string inStr = stripLeadingAndTrailingBraces(recordsend);
            std::vector<std::string> myparts = split(inStr,',');

            string fromFioAddress = "";
            string toFioAddress = "";
            string fioFundsRequestId = "";


            for (std::vector<std::string>::iterator it = myparts.begin() ; it != myparts.end(); ++it)
            {
                string tmpstr = *it;
                //look for the from fio address
                if (fromFioAddress.length() == 0) {
                    std::size_t found = tmpstr.find("fromfioadd");
                    if (found != std::string::npos) {
                        fromFioAddress = getValueAfterColon(tmpstr);
                    }
                }
                //look for the to fio address
                if (toFioAddress.length() == 0) {
                    std::size_t found = tmpstr.find("tofioadd");
                    if (found != std::string::npos) {
                        toFioAddress = getValueAfterColon(tmpstr);
                    }
                }
                //look for the funds request id
                if (fioFundsRequestId.length() == 0) {
                    std::size_t found = tmpstr.find("fioreqid");
                    if (found != std::string::npos) {
                        fioFundsRequestId = getValueAfterColon(tmpstr);
                    }
                }
            }

            //check that names were found in the json.
            fio_400_assert(fromFioAddress.length() > 0, "fromfioadd", fromFioAddress,"from fio address not found in obt json blob", ErrorInvalidJsonInput);
            fio_400_assert(toFioAddress.length() > 0, "tofioadd", toFioAddress,"to fio address not found in obt json blob", ErrorInvalidJsonInput);


            //if the request id is specified in the json then look to see if it is present
            //in the table, if so then add the associated update into the status tables.
            //if the id is present in the json and not inthe table error.
            if (fioFundsRequestId.length() > 0)
            {
                uint64_t currentTime = current_time();
                uint64_t requestId;

                std::istringstream iss(fioFundsRequestId.c_str());
                iss >> requestId;

                auto fioreqctx_iter = fiorequestContextsTable.find(requestId);
                fio_400_assert(fioreqctx_iter != fiorequestContextsTable.end(), "fioreqid", fioFundsRequestId,"No FIO request was found for the specified id ", ErrorRequestContextNotFound);
                //insert a send record into the status table using this id.
                fiorequestStatusTable.emplace(_self, [&](struct fioreqsts &fr) {
                    fr.id = fiorequestStatusTable.available_primary_key();;
                    fr.fioreqid = requestId;
                    fr.status = static_cast<trxstatus >(trxstatus::senttobc);
                    fr.metadata = "";
                    fr.fiotime = currentTime;
                });
            }

            auto fionames = fionames_table(N(fio.system),N(fio.system));

            //check the from address, see that its a valid fio name
            uint64_t nameHash = ::eosio::string_to_uint64_t(fromFioAddress.c_str());
            auto fioname_iter = fionames.find(nameHash);
            fio_400_assert(fioname_iter != fionames.end(), "fromfioadd", fromFioAddress,"FIO name not registered", ErrorFioNameNotRegistered);

            //check the to address, see that its a valid fio name
            nameHash = ::eosio::string_to_uint64_t(toFioAddress.c_str());
            fioname_iter = fionames.find(nameHash);
            fio_400_assert(fioname_iter != fionames.end(), "tofioadd", toFioAddress,"FIO name not registered", ErrorFioNameNotRegistered);

            send_response(recordsend.c_str());
        }

        /***
        * this action will record a send using Obt. the input json will be verified, if verification fails an exception will be thrown.
        * if verification succeeds then status tables will be updated...
        */
        // @abi action
        void newfundsreq(const string &fromfioadd, const string &tofioadd,const string &topubadd,const string &amount,
                         const string &tokencode,const string &metadata,const string &actor) {



            print("call new funds request\n");

            //check that names were found in the json.
            fio_400_assert(fromfioadd.length() > 0, "fromfioadd", fromfioadd,"from fio address not specified", ErrorInvalidJsonInput);
            fio_400_assert(tofioadd.length() > 0, "tofioadd", tofioadd,"to fio address not specified", ErrorInvalidJsonInput);



            auto fionames = fionames_table(N(fio.system),N(fio.system));

            //check the from address, see that its a valid fio name
            uint64_t nameHash = ::eosio::string_to_uint64_t(fromfioadd.c_str());
            auto fioname_iter = fionames.find(nameHash);
            fio_400_assert(fioname_iter != fionames.end(), "fromfioadd", fromfioadd,"FIO name not registered", ErrorFioNameNotRegistered);

            //check the to address, see that its a valid fio name
            nameHash = ::eosio::string_to_uint64_t(tofioadd.c_str());
            fioname_iter = fionames.find(nameHash);
            fio_400_assert(fioname_iter != fionames.end(), "tofioadd", tofioadd,"FIO name not registered", ErrorFioNameNotRegistered);

            //put the thing into the table get the index.
            uint64_t id = fiorequestContextsTable.available_primary_key();



            uint64_t currentTime = current_time();
            uint64_t toHash = ::eosio::string_to_uint64_t(tofioadd.c_str());
            uint64_t fromHash = ::eosio::string_to_uint64_t(fromfioadd.c_str());

            //insert a send record into the status table using this id.
            fiorequestContextsTable.emplace(_self, [&](struct fioreqctxt &frc) {
                frc.fioreqid = id;
                frc.fromfioaddr = fromHash;
                frc.tofioaddr = toHash;
                frc.topubaddr = topubadd;
                frc.amount = amount;
                frc.tokencode = tokencode;
                frc.metadata = "";
                frc.fiotime = currentTime;
            });


            nlohmann::json json = {{"status","OK"},{"fioreqid",id},{"fromfioadd",fromfioadd},{"tofioadd",tofioadd},{"topubadd",topubadd},{"amount",amount},{"metadata",metadata}};
            send_response(json.dump().c_str());
        }
    };
EOSIO_ABI(FioRequestObt, (recordsend)(newfundsreq))

}

