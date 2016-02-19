GPIO_SENSE="gpio-sense"

# make led on by publishing the following delta:
./_update-config.sh $1 \""A|$GPIO_SENSE|2"\" \"2~1\" \"sleep\" 0

# ensure led is off when updating delta to:
./_update-config.sh $1 \"1\" \"$GPIO_SENSE\" 

if ./_ask-user.sh "Is LED on? (because sense value <= threshold-delta ";
then
    echo "Test 3 success"
else 
    echo "Test 3 failure"
    exit 1
fi

# ensure led is off when updating delta to:
./_update-config.sh $1 \"2\" \"$GPIO_SENSE\" 

if ./_ask-user.sh "Is LED on? (because sense value is (threshold-delta;threshold+delta) ";
then
    echo "Test 3 success"
else 
    echo "Test 3 failure"
    exit 1
fi

./_update-config.sh $1 \"3\" \"$GPIO_SENSE\" 

if ./_ask-user.sh "Is LED off? (because sense value is >= threshold + delta)";
then
    echo "Test 1 success"
else 
    echo "Test 1 failure"
    exit 1
fi

# ensure led is on when updating delta to:
./_update-config.sh $1 \"2\" \"$GPIO_SENSE\" 

# ask user if led is on because the sense value is in (threshold-delta : threshold+delta)
if ./_ask-user.sh "Is LED off? (because sense value is in (threshold-delta : threshold+delta))";
then
    echo "Test 2 success"
else 
    echo "Test 2 failure"
    exit 1
fi

