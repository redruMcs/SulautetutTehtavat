*** Settings ***
Library           SerialLibrary
Library           String
Suite Setup       Connect Serial
Suite Teardown    Disconnect Serial

*** Variables ***
${com}      COM5
${baud}     115200
${board}    nRF5340

*** Test Cases ***
Valid Lower Time
    # 000001 -> 1 s -> 1 second	
    Send Time And Check    000001    1

Valid Upper Time
    # 005959 -> 59 min 59 s -> 3599 seconds
    Send Time And Check    005959    3599

Invalid Seconds
    # Seconds 60 > 59 -> TIME_VALUE_ERROR
    Send Time And Check    000060    -3

Invalid Minutes
    # Minutes 60 > 59 -> TIME_VALUE_ERROR
    Send Time And Check    006000    -3

Invalid Hours
    # Hours 24 > 23 -> TIME_VALUE_ERROR
    Send Time And Check    240000    -3

Invalid Length
    # Five characters -> TIME_LEN_ERROR
    Send Time And Check    12345    -1

Invalid Characters
    # Contains letters -> TIME_ARRAY_ERROR
    Send Time And Check    000A10    -2

Invalid Zero Duration
	# Zero duration useless
	Send Time And Check    000000    -3

*** Keywords ***
Connect Serial
    Log To Console    Connecting to ${board} on ${com}
    Add Port    ${com}    baudrate=${baud}    encoding=ascii
    Port Should Be Open    ${com}
    Reset Input Buffer
    Reset Output Buffer

Disconnect Serial
    Log To Console    Disconnecting ${board}
    Delete Port    ${com}

Send Time And Check
    [Arguments]    ${time_string}    ${expected}

    # Clear old serial data before sending the command
    Reset Input Buffer

    # X terminates the incoming command
    Write Data    ${time_string}X    encoding=ascii
    Log To Console    Sent: ${time_string}X

    ${result}=    Set Variable    ${EMPTY}

    # Read lines until the dedicated parser result arrives
    FOR    ${i}    IN RANGE    20
        ${line}=    Read Until    terminator=0A    encoding=ascii
        Log To Console    Serial output: ${line}

        IF    'RESULT:' in $line
            ${result}=    Fetch From Right    ${line}    RESULT:
            ${result}=    Strip String    ${result}
            BREAK
        END
    END

    Should Not Be Empty
    ...    ${result}
    ...    msg=No RESULT line received for ${time_string}

    Log To Console    Parser result: ${result}
    Should Be Equal As Integers    ${result}    ${expected}