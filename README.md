# Build
```bash
cd driver
make
cd ../user
make

# Run
cd scripts
./setup_keylogger.sh   
cd ../user
./keylogger_app        

# Stats
cat /proc/keylogger_stats

# Teardown
cd scripts
./cleanup_keylogger.sh


Project Structre:

  driver/
    keylogger.c
    Makefile
  user/
    keylogger_app.c
    Makefile
  scripts/
    cleanup_keylogger.sh
    setup_keylogger.sh
  README.md

