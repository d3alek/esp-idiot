# requirement python 2.7
# get a working pip installation from your package manager or https://pip.pypa.io/en/stable/installing/
# then install virtualenv (use sudo to install globally)
pip install virtualenv

# recursive to pick up submodule
git clone --recursive https://github.com/d3alek/esp-idiot

# get MQTTConfig.h
scp <MQTTConfig.h-from-host-that-has-it> esp-idiot/src

cd esp-idiot
virtualenv venv
source venv/bin/activate
pip install -r requirements.txt 

# download dependencies, compile and upload to chip (you must have programmer attached)
pio run -t upload
