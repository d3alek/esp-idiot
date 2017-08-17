if [[ -z "$1" ]]; then
    echo "Using default environment zelenik2"
    ENVIRONMENT="zelenik2"
else 
    echo "Using environment $1"
    ENVIRONMENT="$1"
fi


pio run -t upload -e $ENVIRONMENT
