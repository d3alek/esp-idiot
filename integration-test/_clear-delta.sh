#set desired to null? may work, otherwise set arguments to null, provide in arguments the conf values that are going to change in this test
# publish to things/$0/update
#  { "state" : { "desired" : null } }

mosquitto_pub -t "things/$1/update" -m '{ "state" : { "desired" : null }}'
