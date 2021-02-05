#!/bin/bash

con='dex.token'
issuer=masteraychen
to=$issuer

quants="1000000000.00000000 JCC
1000000000.00000000 ICF
300000000.00000000 MAT
1000000000.00000000 LUCK
10000000000.00000000 EHEX
100000000.00000000 OVB
1000000000.00000000 TCC
570000000.00000000 WTG
100000000.00000000 CMC
100000000.00000000 WT"

SAVEIFS=$IFS
IFS=$'\n'
quants=($quants)
IFS=$SAVEIFS

for (( i=0; i<${#quants[@]}; i++ ))
do
    quant="${quants[$i]}"
    echo cl push action $con create "'[\"$to\",\"$quant\"]'" -p $con
    echo cl push action $con issue "'[\"$to\",\"$quant\",\"\"]'" -p $issuer