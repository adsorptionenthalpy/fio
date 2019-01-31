/** Common file implementation for Fio Javascript SDK
 *  Description: FioFinance common file
 *  @author Ciju John
 *  @file fio.common.js
 *  @copyright Dapix
 *
 *  Changes: 10-8-2018 Adam Androulidakis
 */

// var exec = require('child_process').exec;

const util = require('util');
const exec = util.promisify(require('child_process').exec);

// General Configuration parameters
class Config {
}
Config.MaxAccountCreationAttempts=  3;
Config.EosUrl =                     'http://localhost:8889';
Config.KeosdUrl =                   'http://localhost:9899';
Config.SystemAccount =              "fio.system";
Config.SystemAccountKey =           "5KBX1dwHME4VyuUss2sYM25D5ZTDvyYrbEz37UJqwAVAsR4tGuY"; // ${Config.SystemAccount} system account active key
Config.TokenAccount =               "eosio.token"
Config.NewAccountBuyRamQuantity=    "1000.0000 FIO";
Config.NewAccountStakeNetQuantity=  "1000.0000 FIO";
Config.NewAccountStakeCpuQuantity=  "1000.0000 FIO";
Config.NewAccountTransfer=          false;
Config.NameRegisterExpiration=      31561920; // 1 year in seconds
// Config.TestAccount=              "fioname11111";
Config.FioFinanceAccount=           "fio.finance";
Config.FioFinanceAccountKey =           "5KBX1dwHME4VyuUss2sYM25D5ZTDvyYrbEz37UJqwAVAsR4tGuY";
Config.LogLevelTrace = 5
Config.LogLevelDebug = 4
Config.LogLevelInfo = 3
Config.LogLevelWarn = 2
Config.LogLevelError = 1
Config.LogLevel=                    Config.LogLevelInfo;
Config.FinalizationTime=            20;     // time in milliseconds to transaction finalization
Config.pmtson=                      false;

class TrxFee {}
TrxFee.domregiter = 14.0000;   // Fee paid upon the original domain registration/renewal by the user registering. Allows the owner to retain ownership
// of the wallet domain for a period of 1 year or until transfer
TrxFee.nameregister = 1.0000;  // Fee paid upon the original name registration/renewal by the user registering. Allows the owner to retain ownership
// of the wallet name for a period of 1 year or until the expiration date of wallet domain. Re-sets the counter for Fee-free Transaction.
TrxFee.domtransfer = 14.0000;  // Fee paid upon wallet domain transfer of ownership by the transferring user.
TrxFee.nametransfer = .1000;   // Fee paid upon wallet name transfer of ownership by the transferring user.
TrxFee.namelookup = .1000;     // Fee paid for looking up a public address for a given wallet name and coin.
TrxFee.upaddress = .1000;      // Fees paid when wallet name to public address mapping is updated.

// taken associated fees
TrxFee.transfer = .1000;   // Fee paid when FIO token is transferred.

// meta-data associated fees
TrxFee.metadata = .1000;

// Helper static functions
class Helper {

    /***
     * Sleep function
     * @param ms    milliseconds to sleep
     * @returns {Promise}   void promise
     */
    static sleep(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    static async execute(command, debug = true) {
        if (Config.LogLevel > 4) console.log("Enter execute()");
        if (Config.LogLevel > 4) console.log("Start exec " + command);

        const { stdout, stderr } = await exec(command, {maxBuffer: 1024 * 500}); // maximum buffer size is increased to 500KB
        if (Config.LogLevel > 4) console.log("End exec");
        if (debug) {
            console.log('stdout: ' + stdout);
            console.log('stderr: ' + stderr);
        }

        if (Config.LogLevel > 4) console.log("Exit execute()");
        return {stdout, stderr}
    }

    static typeOf( obj ) {
		return  ({}).toString.call( obj ).match(/\s(\w+)/)[1].toLowerCase();
    }

    static checkTypes( args, types ) {
        args = [].slice.call( args );
        for ( var i = 0; i < types.length; ++i ) {
            if ( Helper.typeOf( args[i] ) != types[i] )
                throw new TypeError( 'param '+ i +' must be of type '+ types[i]+ ', found type '+ Helper.typeOf( args[i] ) );
				
			if (args[i] === "") {
				throw new Error('Null or empty parameter' + types[i]);
            }
        }
    }


    static randomString(length, chars) {
        var result = '';
        for (var i = length; i > 0; --i) result += chars[Math.floor(Math.random() * chars.length)];
        return result;
    }
	
	
   	// Get account details
    // Returns tuple [status, eos response]
    static async getAccount(accountName) {
        Helper.checkTypes( arguments, ['string',] );

        const Url=Config.EosUrl + '/v1/chain/get_account';
        const Data=`{"account_name": "${accountName}"}`;

        //optional parameters
        const otherParams={
            headers:{"content-type":"application/json; charset=UTF-8"},
            body:Data,
            method:"POST"
        };

        let result = await fetch(Url, otherParams)
            .then(res => {
                if (!res.ok){
                    throw new FioError(res.json(),'Network response was not ok.');
                }
                return res.json()
            })
            .catch(rej => {
                console.error(`fetch rejection handler.`)
                throw rej;
            });

        return [true, result];
    }

    static async setContract(accountName, contractDir, wasmFile, abiFile) {
        Helper.checkTypes( arguments, ['string', 'string', 'string', 'string'] );

        let command = `programs/cleos/cleos --url ${Config.EosUrl}  --wallet-url ${Config.KeosdUrl} set contract -j ${accountName} ${contractDir} ${wasmFile} ${abiFile}`;
        if (Config.LogLevel > 2) console.log(`Executing command: ${command}`);
        let result = await Helper.execute(command, false)
            .catch(rej => {
                console.error(`execute() promise rejection handler.`);
                throw rej;
            });

        return [true, result];
    }

    static async startup() {
        if (Config.LogLevel > 4) console.log("Enter startup().");

        let result = await Helper.execute("tests/startupNodeos.py", false)
            .catch(rej => {
                console.error(`Helper.execute() promise rejection handler.`);
                throw rej;
            });

        if (Config.LogLevel > 4) console.log("Exit startup()");
        return [true, result];
    }

    static async shutdown() {
        try {
            console.log("Shutting down blockchain and wallet.");
            let result = await Helper.execute("/usr/bin/pkill -9 nodeos", false);
            result = await Helper.execute("/usr/bin/pkill -9 keosd", false);
            return [true, result];
        } catch (e) {
            console.error("Helper.execute() threw exception");
            throw e;
        }
    }

    static Log(logSwitch, ...restArgs) {
        if (logSwitch) {
            console.log(restArgs.toString());
        }
    }

}

// Custom Error with details object. Details is context specific.
class FioError extends Error {
    constructor(details, ...params) {
        // Pass remaining arguments (including vendor specific ones) to parent constructor
        super(...params);

        // Custom debugging information
        this.details = details; // this is a promise with error details
    }
}

module.exports = {Config,TrxFee,Helper,FioError};
