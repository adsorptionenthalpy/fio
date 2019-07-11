#!/usr/bin/env bash



printf "\n\n${bldred}  FFFFFFFFFFFFFFFFFFF IIIIIIIII     OOOOOOO     IIIIIIIII     OOOOOOO\n"
printf "  F:::::::::::::::::F I:::::::I   OO::::::::OO  I:::::::I   OO:::::::OO\n"
printf "  FF:::::FFFFFFFF:::F II:::::II O:::::OOO:::::O II:::::II O:::::OOO:::::O\n"
printf "    F::::F      FFFFF   I:::I  O:::::O   O:::::O  I:::I  O:::::O   O:::::O\n"
printf "    F::::F              I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F:::::FFFFFFFFF     I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F:::::::::::::F     I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F:::::FFFFFFFFF     I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F::::F              I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F::::F              I:::I  O:::::O   O:::::O  I:::I  O:::::O   O:::::O\n"
printf "  FF::::::FF          II:::::II O:::::OOO:::::O II:::::II O:::::OOO:::::O\n"
printf "  F:::::::FF          I:::::::I   OO:::::::OO   I:::::::I   OO:::::::OO\n"
printf "  FFFFFFFFFF          IIIIIIIII     OOOOOOO     IIIIIIIII     OOOOO0O \n${txtrst}"

echo 'Welcome to the Basic Environment'

restartneeded=0
oldpath=$PWD

if [ -f ../walletkey.ini ]; then
    echo $'Restart Detected\n'
    restartneeded=1
fi

if [ -z "$1" ]; then
    read -p $'1. Local Blockchain 2. Build Contracts 3. Nuke All\nChoose(#):' mChoice
else
    mChoice=$1
fi

if [ $mChoice == 2 ]; then
    pwd
    cd ../fio.contracts
    ./build.sh

       echo COPYING ABI FILES FROM contracts TO ./build/contracts!
            cp ./contracts/fio.fee/fio.fee.abi ./build/contracts/fio.fee/fio.fee.abi
            cp ./contracts/fio.name/fio.name.abi ./build/contracts/fio.name/fio.name.abi
            cp ./contracts/fio.request.obt/fio.request.obt.abi ./build/contracts/fio.request.obt/fio.request.obt.abi
    exit -1
fi

if [ $mChoice == 1 ]; then
    if [ -z "$1" ]; then
        read -p $'1. Local ( DEVELOPMENT ) 2. AWS ( FULL SYSTEM )\nChoose(#):' yChoice
    else
        yChoice=$1
    fi

    #EOSIO Directory Check
    if [ -f ../fio.contracts/build/contracts/eosio.bios/eosio.bios.wasm ]; then
        eosio_bios_contract_name_path="$PWD/../fio.contracts/build/contracts/eosio.bios"
    else
        echo 'No wasm file found at $PWD/build/contracts/eosio.bios'
    fi

    if [ -f ../fio.contracts/build/contracts/fio.system/fio.system.wasm ]; then
        fio_system_contract_name_path="$PWD/../fio.contracts/build/contracts/fio.system"
    else
        echo 'No wasm file found at $PWD/build/contracts/fio.system'
    fi

    if [ -f ../fio.contracts/build/contracts/eosio.msig/eosio.msig.wasm ]; then
        eosio_msig_contract_name_path="$PWD/../fio.contracts/build/contracts/eosio.msig"
    else
        echo 'No wasm file found at $PWD/build/contracts/eosio.msig'
    fi

    if [ -f ../fio.contracts/build/contracts/fio.token/fio.token.wasm ]; then
        fio_token_contract_name_path="$PWD/../fio.contracts/build/contracts/fio.token"
    else
        echo 'No wasm file found at $PWD/build/contracts/fio.token'
    fi
    #Fio Name Directory Check
    if [ -f ../fio.contracts/build/contracts/fio.name/fio.name.wasm ]; then
        fio_contract_name_path="$PWD/../fio.contracts/build/contracts/fio.name"
    else
        echo 'No wasm file found at $PWD/build/contracts/fio.name'
    fi

    if [ -f ../fio.contracts/build/contracts/fio.fee/fio.fee.wasm ]; then
        fio_fee_name_path="$PWD/../fio.contracts/build/contracts/fio.fee"
    else
        echo 'No wasm file found at $PWD/build/contracts/fio.fee'
    fi

    if [ -f ../fio.contracts/build/contracts/fio.request.obt/fio.request.obt.wasm ]; then
            fio_reqobt_name_path="$PWD/../fio.contracts/build/contracts/fio.request.obt"
        else
            echo 'No wasm file found at $PWD/build/contracts/fio.request.obt'
    fi

    if [ -f ../fio.contracts/build/contracts/fio.tpid/fio.tpid.wasm ]; then
            fio_tpid_name_path="$PWD/../fio.contracts/build/contracts/fio.tpid"
        else
            echo 'No wasm file found at $PWD/build/contracts/fio.tpid'
    fi

    sleep 2s
    cd ~/opt/eosio/bin

    if [ ! -f $oldpath/../walletkey.ini ]; then
        #./keosd --config-dir ~/eosio-wallet --wallet-dir ~/eosio-wallet --http-server-address localhost:8879 --http-alias localhost:8900 > ~/eosio-wallet/stdout.log 2> ~/eosio-wallet/stderr.log &
        ./cleos wallet create -n fio -f $oldpath/../walletkey.ini
    else
        walletkey=$(head -n 1 $oldpath/../walletkey.ini)
        echo 'Using Password:' $walletkey
        sleep 2s
        #./keosd --config-dir ~/eosio-wallet --wallet-dir ~/eosio-wallet --http-server-address localhost:8879 --http-alias localhost:8900 > ~/eosio-wallet/stdout.log 2> ~/eosio-wallet/stderr.log &
        ./cleos wallet unlock -n fio --password $walletkey
    fi

    if [ $restartneeded == 0 ]; then
        #Import Wallet Keys
        ./cleos wallet import --private-key 5JxUfAkXoCQdeZKNMhXEqRkFcZMYa3KR3vbie7SKsPv6rS3pCHg -n fio
        ./cleos wallet import --private-key 5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3 -n fio
        ./cleos wallet import --private-key 5KDQzVMaD1iUdYDrA2PNK3qEP7zNbUf8D41ZVKqGzZ117PdM5Ap -n fio
        ./cleos wallet import --private-key 5Jr2SxVH6bh6QcJerJrGKvNAp7zfemN98rp4BfzFonkJQmcumvP -n fio
        ./cleos wallet import --private-key 5KBX1dwHME4VyuUss2sYM25D5ZTDvyYrbEz37UJqwAVAsR4tGuY -n fio
        ./cleos wallet import --private-key 5JLxoeRoMDGBbkLdXJjxuh3zHsSS7Lg6Ak9Ft8v8sSdYPkFuABF -n fio
        ./cleos wallet import --private-key 5HvaoRV9QrbbxhLh6zZHqTzesFEG5vusVJGbUazFi5xQvKMMt6U -n fio
        ./cleos wallet import --private-key 5JCpqkvsrCzrAC3YWhx7pnLodr3Wr9dNMULYU8yoUrPRzu269Xz -n fio
        ./cleos wallet import --private-key 5K3s1yePVjrsCJbeyoXGAfJn9yCKKZi6E9oyw59mt5LaJG3Bm1N -n fio
        ./cleos wallet import --private-key 5JAs6QTmEyVjJGbQf1e5MjWLoNqcUAMyZLbLwvDhaG3gYBawDwK -n fio
        ./cleos wallet import --private-key 5JvWX1RkiaRS7jgMTb9oZsFKy77jJHkzEv78JRFkjxdDQR3d6wN -n fio
        ./cleos wallet import --private-key 5JKQTYUjxWQbWt53z6Prrb3PUNtEiG5GcyXv3yHTK1nJaxzABrk -n fio
        ./cleos wallet import --private-key 5K7Dah3CB17j6hpQ5vnBebN2yzGkt5K6A2xV7KshKyAfAUJyY8J -n fio
        ./cleos wallet import --private-key 5HvvvAF32bADDzyqcc3kRN4GVKGAZdVFXQexDScJEqTnpk29Kdv -n fio
        ./cleos wallet import --private-key 5JUgV5evcQGJkniK7rH1HckBAvRKsJP1Zgbv39UWnyTBEkMyWMj -n fio
        ./cleos wallet import --private-key 5K2HBexbraViJLQUJVJqZc42A8dxkouCmzMamdrZsLHhUHv77jF -n fio
        ./cleos wallet import --private-key 5JA5zQkg1S59swPzY6d29gsfNhNPVCb7XhiGJAakGFa7tEKSMjT -n fio
        ./cleos wallet import --private-key 5KQPk78a4Srm11NnuxGnWGNJuZtGaMErQ31XFDvXwzyS3Ru7AJQ -n fio
        ./cleos wallet import --private-key 5JqMPkY5AZMYe4LSu1byGPbyjwin9uE9uB3a14WC8ULtYvjaJzE -n fio
        ./cleos wallet import --private-key 5KcNHxGqm9csmFq8VPa5dGHZ99H8qDGj5F5yWgg6u3D4PUkUoEp -n fio
        ./cleos wallet import --private-key 5KDDv35PHWGkReThhNsj2j23DCJzvWTLvZbSTUj8BFNicrJXPqG -n fio
        ./cleos wallet import --private-key 5JHsotwiGMAjrTu3oCiXYjzJ3dEz9mU7HFc4UigvWgEksbpaVkc -n fio
        ./cleos wallet import --private-key 5K8dPJgVYWDxfEURh1cvrRzqc7uvDLqNHjxs83Rf2n8Hu7rqhmp -n fio
        ./cleos wallet import --private-key 5JDGpznJSJw8cKQYtEuctobAu2zVmD3Rw3fWwPzDuF4XbriseLy -n fio
        ./cleos wallet import --private-key 5JiSAASb4PZTajemTr4KTgeNBWUxrz6SraHGUvNihSskDA9aY5b -n fio
        ./cleos wallet import --private-key 5JwttkBMpCijBWiLx75hHTkYGgDm5gmny7nvnss4s1FoZWPxNui -n fio
    fi

    #Start Both Nodes
    if pgrep -x "nodeos" > /dev/null
    then
        kill nodeos
    fi

    ./nodeos --http-server-address localhost:8879 --http-validate-host=0 --enable-stale-production --producer-name eosio --plugin eosio::chain_api_plugin --plugin eosio::net_api_plugin 2> $oldpath/../node1.txt &
    sleep 3s
    ./nodeos --max-transaction-time=6000 --producer-name inita --plugin eosio::chain_api_plugin --plugin eosio::net_api_plugin --http-server-address 0.0.0.0:8889 --http-validate-host=0 --p2p-listen-endpoint :9877 --p2p-peer-address 0.0.0.0:9876 --config-dir $HOME/node2 --data-dir $HOME/node2 --private-key [\"FIO79vbwYtjhBVnBRYDjhCyxRFVr6JsFfVrLVhUKoqFTnceZtPvAU\",\"5JxUfAkXoCQdeZKNMhXEqRkFcZMYa3KR3vbie7SKsPv6rS3pCHg\"] --contracts-console 2> $oldpath/../node2.txt &
    sleep 6s

    if [ $restartneeded == 0 ]; then
        #Create Accounts

        echo $'Creating Accounts...\n'
        ./cleos -u http://localhost:8889 create account eosio fio.token FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio fio.system FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio fio.fee FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio fio.reqobt FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio fio.tpid FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio fio.treasury FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio eosio.bpay FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio eosio.msig FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio eosio.ram FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio eosio.ramfee FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio eosio.saving FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio eosio.stake FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS
        ./cleos -u http://localhost:8889 create account eosio eosio.vpay FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS

        #Set Contacts
        ./cleos -u http://localhost:8889 set contract fio.token $fio_token_contract_name_path fio.token.wasm fio.token.abi
        sleep 2s
        ./cleos -u http://localhost:8889 set contract fio.tpid $fio_tpid_name_path fio.tpid.wasm fio.tpid.abi
        ./cleos -u http://localhost:8889 set contract eosio.msig $eosio_msig_contract_name_path eosio.msig.wasm eosio.msig.abi
        ./cleos -u http://localhost:8889 set contract -j fio.system $fio_contract_name_path fio.name.wasm fio.name.abi --permission fio.system@active

        sleep 5s

        echo bound all contracts

        ./cleos -u http://localhost:8889 push action -j fio.token create '["eosio","1000000000.000000000 FIO"]' -p fio.token@active

        if [ $yChoice == 2 ]; then

            ./cleos -u http://localhost:8889 set contract -j eosio $fio_system_contract_name_path fio.system.wasm fio.system.abi --permission eosio@active
            sleep 3s
        fi

        ./cleos -u http://localhost:8889 set contract eosio $eosio_bios_contract_name_path eosio.bios.wasm eosio.bios.abi

        sleep 3s
        # Bind more fio contracts
        ./cleos -u http://localhost:8889 set contract -j fio.reqobt $fio_reqobt_name_path fio.request.obt.wasm fio.request.obt.abi --permission fio.reqobt@active
        sleep 3s
        ./cleos -u http://localhost:8889 set contract -j fio.fee $fio_fee_name_path fio.fee.wasm fio.fee.abi --permission fio.fee@active
        sleep 3s

        ./cleos -u http://localhost:8889 create account eosio r41zuwovtn44 FIO5oBUYbtGTxMS66pPkjC2p8pbA3zCtc8XD4dq9fMut867GRdh82 FIO5oBUYbtGTxMS66pPkjC2p8pbA3zCtc8XD4dq9fMut867GRdh82
        ./cleos -u http://localhost:8889 create account eosio htjonrkf1lgs FIO7uRvrLVrZCbCM2DtCgUMospqUMnP3JUC1sKHA8zNoF835kJBvN FIO7uRvrLVrZCbCM2DtCgUMospqUMnP3JUC1sKHA8zNoF835kJBvN
        ./cleos -u http://localhost:8889 create account eosio euwdcp13zlrj FIO8NToQB65dZHv28RXSBBiyMCp55M7FRFw6wf4G3GeRt1VsiknrB FIO8NToQB65dZHv28RXSBBiyMCp55M7FRFw6wf4G3GeRt1VsiknrB
        ./cleos -u http://localhost:8889 create account eosio mnvcf4v1flnn FIO5GpUwQtFrfvwqxAv24VvMJFeMHutpQJseTz8JYUBfZXP2zR8VY FIO5GpUwQtFrfvwqxAv24VvMJFeMHutpQJseTz8JYUBfZXP2zR8VY

        ./cleos -u http://localhost:8889 push action -j fio.token issue '["r41zuwovtn44","1000000.000000000 FIO","memo"]' -p eosio@active
        ./cleos -u http://localhost:8889 push action -j fio.token issue '["htjonrkf1lgs","1000.000000000 FIO","memo"]' -p eosio@active
        ./cleos -u http://localhost:8889 push action -j fio.token issue '["euwdcp13zlrj","1000.000000000 FIO","memo"]' -p eosio@active
        ./cleos -u http://localhost:8889 push action -j fio.token issue '["mnvcf4v1flnn","1000.000000000 FIO","memo"]' -p eosio@active
    fi
    if [ $restartneeded == 0 ]; then
          # Setup permissions for inline actions in the
          echo Setting up inline permissions
          echo Adding eosio code to fio.token and fio.system.
          # Reference: https://github.com/EOSIO/eos/issues/4348#issuecomment-400562839
          # Reference: https://developers.eos.io/eosio-home/docs/inline-actions
          ./cleos -u http://localhost:8889 set account permission fio.token active '{"threshold": 1,"keys": [{"key": "FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS","weight": 1}],"accounts": [{"permission":{"actor":"fio.token","permission":"eosio.code"},"weight":1}]}}' owner -p fio.token@owner
          #make the fio.token into a privileged account
          ./cleos -u http://localhost:8889 push action eosio setpriv '["fio.token",1]' -p eosio@active
          ./cleos -u http://localhost:8889 set account permission fio.system active '{"threshold": 1,"keys": [{"key": "FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS","weight": 1}],"accounts": [{"permission":{"actor":"fio.system","permission":"eosio.code"},"weight":1}]}}' owner -p fio.system@owner
          #make the fio.system into a privileged account
          ./cleos -u http://localhost:8889 push action eosio setpriv '["fio.system",1]' -p eosio@active
          ./cleos -u http://localhost:8889 set account permission fio.reqobt active '{"threshold": 1,"keys": [{"key": "FIO7isxEua78KPVbGzKemH4nj2bWE52gqj8Hkac3tc7jKNvpfWzYS","weight": 1}],"accounts": [{"permission":{"actor":"fio.reqobt","permission":"eosio.code"},"weight":1}]}}' owner -p fio.reqobt@owner
          #make the fio.reqobt into a privileged account
          ./cleos -u http://localhost:8889 push action eosio setpriv '["fio.reqobt",1]' -p eosio@active

          ./cleos -u http://localhost:8889 set account permission r41zuwovtn44 active '{"threshold":1,"keys":[{"key":"FIO5oBUYbtGTxMS66pPkjC2p8pbA3zCtc8XD4dq9fMut867GRdh82","weight":1}],"accounts":[{"permission":{"actor":"eosio","permission":"eosio.code"},"weight":1}]}' owner -p r41zuwovtn44@owner
          ./cleos -u http://localhost:8889 push action eosio setpriv '["r41zuwovtn44",1]' -p eosio@active

     fi

    #create fees for the fio protocol
     echo "creating fees"

     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"register_fio_domain","type":"0","suf_amount":"30000000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"register_fio_address","type":"0","suf_amount":"1000000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"renew_fio_domain","type":"0","suf_amount":"30000000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"renew_fio_address","type":"0","suf_amount":"1000000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"add_pub_address","type":"1","suf_amount":"100000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"transfer_tokens_pub_key","type":"0","suf_amount":"250000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"transfer_tokens_fio_address","type":"0","suf_amount":"100000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"new_funds_request","type":"1","suf_amount":"100000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"reject_funds_request","type":"1","suf_amount":"100000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"record_send","type":"1","suf_amount":"100000000"}' --permission fio.fee@active
     ./cleos -u http://localhost:8889 push action -j fio.fee create '{"end_point":"set_fio_domain_public","type":"1","suf_amount":"10000000"}' --permission fio.fee@active

    echo setting accounts
    sleep 1
    dom=1
    retries=3

    ./cleos -u http://localhost:8889 push action -j fio.system regdomain '{"fio_domain":"dapix","owner_fio_public_key":"","max_fee":"40000000000","actor":"r41zuwovtn44","tpid":""}' --permission r41zuwovtn44@active
    sleep 5

    #Create Account Name
    ./cleos -u http://localhost:8889 push action -j fio.system regaddress '{"fio_address":"casey:dapix","owner_fio_public_key":"","max_fee":"40000000000","actor":"r41zuwovtn44","tpid":""}' --permission r41zuwovtn44@active
    ./cleos -u http://localhost:8889 push action -j fio.system regaddress '{"fio_address":"adam:dapix","owner_fio_public_key":"","max_fee":"40000000000","actor":"r41zuwovtn44","tpid":""}' --permission r41zuwovtn44@active
    ./cleos -u http://localhost:8889 push action -j fio.system regaddress '{"fio_address":"ed:dapix","owner_fio_public_key":"","max_fee":"40000000000","actor":"r41zuwovtn44","tpid":"adam.dapix"}' --permission r41zuwovtn44@active

elif [ $mChoice == 3 ]; then
    read -p $'WARNING: ALL FILES ( WALLET & CHAIN ) WILL BE DELETED\n\nContinue? (1. Yes 2. No): ' bChoice

    if [ $bChoice == 1 ]; then
        cd $oldpath
         ./chain_nuke.sh

        cd ~/opt/eosio/bin
        ./nodeos --hard-replay

        echo $'\nNUKE COMPLETE - WELCOME TO YOUR NEW BUILD'
    fi

    exit 1
else
    exit 1
fi

printf "\n\n${bldgrn}  FFFFFFFFFFFFFFFFFFF IIIIIIIII     OOOOOOO     IIIIIIIII     OOOOOOO\n"
printf "  F:::::::::::::::::F I:::::::I   OO::::::::OO  I:::::::I   OO:::::::OO\n"
printf "  FF:::::FFFFFFFF:::F II:::::II O:::::OOO:::::O II:::::II O:::::OOO:::::O\n"
printf "    F::::F      FFFFF   I:::I  O:::::O   O:::::O  I:::I  O:::::O   O:::::O\n"
printf "    F::::F              I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F:::::FFFFFFFFF     I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F:::::::::::::F     I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F:::::FFFFFFFFF     I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F::::F              I:::I  O::::O     O::::O  I:::I  O::::O     O::::O\n"
printf "    F::::F              I:::I  O:::::O   O:::::O  I:::I  O:::::O   O:::::O\n"
printf "  FF::::::FF          II:::::II O:::::OOO:::::O II:::::II O:::::OOO:::::O\n"
printf "  F:::::::FF          I:::::::I   OO:::::::OO   I:::::::I   OO:::::::OO\n"
printf "  FFFFFFFFFF          IIIIIIIII     OOOOOOO     IIIIIIIII     OOOOO0O \n${txtrst}"
echo $'\nTest Environment Setup Completed\n'
