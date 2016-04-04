#!/bin/bash

source `dirname $0`/build-opm-core.sh

ERT_REVISION=master
OPM_COMMON_REVISION=master
OPM_PARSER_REVISION=master
OPM_MATERIAL_REVISION=master
OPM_CORE_REVISION=$sha1

if grep -q "ert=" <<< $ghprbCommentBody
then
  ERT_REVISION=pull/`echo $ghprbCommentBody | sed -r 's/.*ert=([0-9]+).*/\1/g'`/merge
fi

if grep -q "opm-common=" <<< $ghprbCommentBody
then
  OPM_COMMON_REVISION=pull/`echo $ghprbCommentBody | sed -r 's/.*opm-common=([0-9]+).*/\1/g'`/merge
fi

if grep -q "opm-parser=" <<< $ghprbCommentBody
then
  OPM_PARSER_REVISION=pull/`echo $ghprbCommentBody | sed -r 's/.*opm-parser=([0-9]+).*/\1/g'`/merge
fi

if grep -q "opm-material=" <<< $ghprbCommentBody
then
  OPM_MATERIAL_REVISION=pull/`echo $ghprbCommentBody | sed -r 's/.*opm-material=([0-9]+).*/\1/g'`/merge
fi

echo "Building with ert=$ERT_REVISION opm-common=$OPM_COMMON_REVISION opm-parser=$OPM_PARSER_REVISION opm-material=$OPM_MATERIAL_REVISION opm-core=$OPM_CORE_REVISION"

build_opm_core
test $? -eq 0 || exit 1

cp serial/build-opm-core/testoutput.xml .
