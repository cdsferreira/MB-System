#!/opt/local/bin/bash
##/bin/bash

# Combine multiple plot images into PDF

# Variable Naming Convention:
# QP_ prefix denotes environment/variables that qplot depends upon (don't modify)
# QU_ prefix for user environment prevents naming conflicts (may be imported/exported, i.e. shared between files)
# QX_ prefix denotes local/application-specific variables (file scope)

# import shared environment
source qp-shared.conf.sh

# define variables
QU_SESSION_ID="" #"-`date +%s`
QU_COMB_IMG_NAME="mbtrnpp-comb"

export QU_MBTRNPP_PROFILE_OIMG_NAME="mbtrnpp-prof"
export QU_MBTRNPP_EVENTS_OIMG_NAME="mbtrnpp-events"
export QU_MBTRNPP_ERRORS_OIMG_NAME="mbtrnpp-errors"
export QU_TRNSVR_PROFILE_OIMG_NAME="trnsvr-prof"
export QU_TRNSVR_CLI_OIMG_NAME="trnsvr-cli"
export QU_TRNUSVR_PROFILE_OIMG_NAME="trnusvr-prof"
export QU_TRNUSVR_CLI_OIMG_NAME="trnusvr-cli"
export QU_MB1SVR_PROFILE_OIMG_NAME="mb1svr-prof"
export QU_MB1SVR_CLI_OIMG_NAME="mb1svr-cli"
export QU_ESTMLE_OIMG_NAME="est-mle"
export QU_ESTMSE_OIMG_NAME="est-mse"
export QU_ESTVAR_OIMG_NAME="est-var"
export QU_MLEDAT_OIMG_NAME="mle-dat"
export QU_MLEX_OIMG_NAME="mle-x"
export QU_MLEY_OIMG_NAME="mle-y"
export QU_MLEZ_OIMG_NAME="mle-z"
export QU_MSEDAT_OIMG_NAME="mmse-dat"
export QU_MSEX_OIMG_NAME="mmse-x"
export QU_MSEY_OIMG_NAME="mmse-y"
export QU_MSEZ_OIMG_NAME="mmse-z"
export QU_PTDAT_OIMG_NAME="pt-dat"
export QU_PTX_OIMG_NAME="pt-x"
export QU_PTY_OIMG_NAME="pt-y"
export QU_PTZ_OIMG_NAME="pt-z"
export QU_TRNSTATE1_OIMG_NAME="trn-state-1"
export QU_TRNSTATE2_OIMG_NAME="trn-state-2"

# define image paths (uses environment from qp-shared.conf)
MBTRNPP_PROFILE="${QP_OUTPUT_DIR}/${QU_MBTRNPP_PROFILE_OIMG_NAME}.${QU_IMG_TYPE}"
MBTRNPP_EVENTS="${QP_OUTPUT_DIR}/${QU_MBTRNPP_EVENTS_OIMG_NAME}.${QU_IMG_TYPE}"
MBTRNPP_ERRORS="${QP_OUTPUT_DIR}/${QU_MBTRNPP_ERRORS_OIMG_NAME}.${QU_IMG_TYPE}"
TRNSVR_PROFILE="${QP_OUTPUT_DIR}/${QU_TRNSVR_PROFILE_OIMG_NAME}.${QU_IMG_TYPE}"
TRNSVR_CLI="${QP_OUTPUT_DIR}/${QU_TRNSVR_CLI_OIMG_NAME}.${QU_IMG_TYPE}"
TRNUSVR_PROFILE="${QP_OUTPUT_DIR}/${QU_TRNUSVR_PROFILE_OIMG_NAME}.${QU_IMG_TYPE}"
TRNUSVR_CLI="${QP_OUTPUT_DIR}/${QU_TRNUSVR_CLI_OIMG_NAME}.${QU_IMG_TYPE}"
MB1SVR_PROFILE="${QP_OUTPUT_DIR}/${QU_MB1SVR_PROFILE_OIMG_NAME}.${QU_IMG_TYPE}"
MB1SVR_CLI="${QP_OUTPUT_DIR}/${QU_MB1SVR_CLI_OIMG_NAME}.${QU_IMG_TYPE}"
ESTMSE="${QP_OUTPUT_DIR}/${QU_ESTMSE_OIMG_NAME}.${QU_IMG_TYPE}"
ESTMLE="${QP_OUTPUT_DIR}/${QU_ESTMLE_OIMG_NAME}.${QU_IMG_TYPE}"
ESTVAR="${QP_OUTPUT_DIR}/${QU_ESTVAR_OIMG_NAME}.${QU_IMG_TYPE}"
MLEDAT="${QP_OUTPUT_DIR}/${QU_MLEDAT_OIMG_NAME}.${QU_IMG_TYPE}"
MSEDAT="${QP_OUTPUT_DIR}/${QU_MSEDAT_OIMG_NAME}.${QU_IMG_TYPE}"
PTDAT="${QP_OUTPUT_DIR}/${QU_PTDAT_OIMG_NAME}.${QU_IMG_TYPE}"
MLEX="${QP_OUTPUT_DIR}/${QU_MLEX_OIMG_NAME}.${QU_IMG_TYPE}"
MLEY="${QP_OUTPUT_DIR}/${QU_MLEY_OIMG_NAME}.${QU_IMG_TYPE}"
MLEZ="${QP_OUTPUT_DIR}/${QU_MLEZ_OIMG_NAME}.${QU_IMG_TYPE}"
MSEX="${QP_OUTPUT_DIR}/${QU_MSEX_OIMG_NAME}.${QU_IMG_TYPE}"
MSEY="${QP_OUTPUT_DIR}/${QU_MSEY_OIMG_NAME}.${QU_IMG_TYPE}"
MSEZ="${QP_OUTPUT_DIR}/${QU_MSEZ_OIMG_NAME}.${QU_IMG_TYPE}"
PTX="${QP_OUTPUT_DIR}/${QU_PTX_OIMG_NAME}.${QU_IMG_TYPE}"
PTY="${QP_OUTPUT_DIR}/${QU_PTY_OIMG_NAME}.${QU_IMG_TYPE}"
PTZ="${QP_OUTPUT_DIR}/${QU_PTZ_OIMG_NAME}.${QU_IMG_TYPE}"
TRN_STATE1="${QP_OUTPUT_DIR}/${QU_TRNSTATE1_OIMG_NAME}.${QU_IMG_TYPE}"
TRN_STATE2="${QP_OUTPUT_DIR}/${QU_TRNSTATE2_OIMG_NAME}.${QU_IMG_TYPE}"

# list of images to combine, in specified order
QU_FILE_LIST="${ESTMSE}|${ESTVAR}|${ESTMLE}|${MSEDAT}|${MSEX}|${MSEY}|${MSEZ}|${PTDAT}|${PTX}|${PTY}|${PTZ}|${MLEDAT}|${MLEX}|${MLEY}|${MLEZ}|${MBTRNPP_PROFILE}|${MBTRNPP_EVENTS}|${MBTRNPP_ERRORS}|${TRNSVR_PROFILE}|${TRNSVR_CLI}|${TRNUSVR_PROFILE}|${TRNUSVR_CLI}|${MB1SVR_PROFILE}|${MB1SVR_CLI}|${TRN_STATE1}|${TRN_STATE2}"

# Set some job name (key)
declare -a QU_KEYS=( "comb-all" )


#### BEGIN QPLOT CONFIGURATION VALUES ####

# Define the PDF combiner job
QP_JOB_DEFS["${QU_KEYS[0]}"]="combine,png,${QU_COMB_IMG_NAME}${QU_SESSION_ID}.pdf,${QU_FILE_LIST}"

# use QP_JOB_ORDER to set default job order
# [if not specified on command line w/ -j option]
# NOTE: plot combiner job is last, since plots must be completed first

# Set job order
QP_JOB_ORDER[${#QP_JOB_ORDER[*]}]="${QU_KEYS[0]}"
