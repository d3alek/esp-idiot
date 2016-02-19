echo $1
select yn in "Yes" "No"; do
    case $yn in
        Yes ) exit 0; break;;
        No ) exit 1;;
    esac
done
