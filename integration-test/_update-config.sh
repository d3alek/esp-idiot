# iterate over arguments, setting $1:$2, $3 = $4 and so on, creating string DELTA
DELTA=""
for i in $(seq 2 2 $#)
do
    next=$((i+1))
    key=${!i}
    value=${!next}
    echo "Argument pair: " $key $value
    if [ -z "$DELTA" ]; then
        DELTA="$key:$value"
    else
        DELTA="$DELTA, $key:$value"
    fi
done

echo $DELTA
mosquitto_pub -t "things/$1/update" -m "{\"state\" : { \"desired\" : { \"config\" : { $DELTA } } } }"

EXIT_CODE=0

RESULT=$(mosquitto_sub -t "things/$1/update" -C 1 -R);

if echo $RESULT | grep "reported" -q; then
    echo "Reported state after update: " $RESULT
    # verify the arguments match by searching the string for $1:$2 and so on
    for i in $(seq 2 2 $#)
    do
        next=$((i+1))
        key=${!i}
        value=${!next}
        if echo $RESULT | grep $key:$value -q; then
            echo "Found correctly set" $key:$value
        else
            echo "ERROR: Did not find correctly set" $key:$value
            EXIT_CODE=1
        fi
    done
else 
    echo "ERROR: Received unexpected message: " $RESULT
    EXIT_CODE=1
fi

./_clear-delta.sh $1

exit $EXIT_CODE
