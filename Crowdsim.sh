#!/bin/bash
if [ ! -f maps/CURRENT$1 ]; then
  rm -f maps/*CURRENT*
  echo "Generating map..."
  python src/mapreader.py $1
  ret=$?
  if [ $ret -ne 0 ]; then
    echo "Skipping compliation..."
    exit
  fi
  echo "Map generated!"    
  echo "Compiling test program..."
  if g++ -o testMap src/testMap.cpp; then 
    echo "Test program compiled!";
  else 
    echo "Test program compilation resulted in error :(";
    echo "Likely that the map was generated incorrectly."
    exit
  fi
  echo "Testing map.h..."
  if ./testMap; then
    echo "map.h is good to go!"
    rm -f testMap
    echo "Compiling main program..."
    mkdir executable
    if qmake; make clean; make -j12; then 
      echo "Program compiled!";
    else 
      echo "Program compilation resulted in error :(";
      exit
    fi
  else
    echo "map.h resulted in errors, panic (likely something else has manipulated map.h, this shouldn't be possible if compilation succeeded, which it apparently did."
    rm -f testMap
    exit
  fi
  else
    echo "Program already setup for use with $1"
fi
touch maps/CURRENT$1
if [[ $1 ]]; then
  executable/./Crowdsim $1
else
  executable/./Crowdsim 0
fi
