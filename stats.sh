#!/bin/sh
#
# libplayer statistics script - (c) 2008 Mathieu Schroeter

ECHO=echo
[ -f /bin/echo ] && ECHO=/bin/echo

bargraph ()
{
  local PERCENT J I

  PERCENT=$1

  J=44
  I=`expr $PERCENT \* $J / 100`
  BARGRAPH=""
  while [ $J -ne 0 ]
  do
    if [ $I -ne 0 ]; then
      BARGRAPH="$BARGRAPH#"
      I=`expr $I - 1`
    else
      BARGRAPH="${BARGRAPH}."
    fi
    J=`expr $J - 1`
  done
}

varlist ()
{
  local VAR I PRE

  VAR=$1

  I=0
  PRE=""
  LIST=""
  for x in $VAR
  do
    if [ "$I" = "6" ]; then
      LIST="$LIST$PRE$x,\n\t\t\t\t"
      PRE=""
    else
      LIST="$LIST$PRE$x"
      PRE=", "
    fi
    I=`expr $I + 1`
  done
}

imp_functions ()
{
  local NAME TOTAL NB_NULL PERCENT I J BARGRAPH

  NAME=$1
  TOTAL=$2

  NB_OK=`grep -h -c "funcs->.*$NAME.*" $wrapper`
  NB_NULL=`expr $TOTAL - $NB_OK`
  PERCENT=`expr $NB_OK \* 100 / $TOTAL`

  bargraph $PERCENT
  $ECHO -e "     $NAME\t: ($NB_OK)\t$PERCENT% \t[$BARGRAPH]"
}

sup_output ()
{
  local NAME LIST TYPE TOTAL PERCENT PATTERN I VAR

  TYPE=$1
  NAME=$2
  TOTAL=$3
  PATTERN=$4

  NB_OK=`grep PLAYER_${TYPE}_ $wrapper | sed "s%$PATTERN%\1%" | sort -u | grep ".*" -c`
  PERCENT=`expr $NB_OK \* 100 / $TOTAL`
  VAR=`grep PLAYER_${TYPE}_ $wrapper | sed "s%$PATTERN%\1%" | sort -u`

  varlist "$VAR"
  $ECHO -e "     $NAME\t: ($NB_OK)\t$PERCENT%\t$LIST"
}

sup_resource ()
{
  local NAME LIST TOTAL PERCENT I PATTERN VAR

  NAME=$1
  TOTAL=$2
  PATTERN=$3

  NB_OK=`grep MRL_RESOURCE_ $wrapper | sed "s%$PATTERN%\1%" | sort -u | grep ".*" -c`
  PERCENT=`expr $NB_OK \* 100 / $TOTAL`
  VAR=`grep MRL_RESOURCE_ $wrapper | sed "s%$PATTERN%\1%" | sort -u`

  varlist "$VAR"
  $ECHO -e "     $NAME\t: ($NB_OK)\t$PERCENT%\t$LIST"
}

$ECHO -e ""
$ECHO -e " libplayer-r`hg tip --template={rev}`\t\t\t\t\t\t\t`date +%Y"-"%m"-"%d`"
$ECHO -e ""
$ECHO -e " These statistics are not relevant. But you can see approximately which"
$ECHO -e " is the progress for every wrapper."

WRAPPER_LIST=`ls src/wrapper_*.c | grep -v dummy`

$ECHO -e ""
$ECHO -e ""
$ECHO -e "  Implemented Functions:"
$ECHO -e "  ~~~~~~~~~~~~~~~~~~~~~~"
$ECHO -e ""

TOTAL_FCT=`grep -h -c "funcs->" src/wrapper_dummy.c`
I=0
for wrapper in $WRAPPER_LIST;
do
  NAME=`$ECHO -e $wrapper | sed "s%.*_\(.*\)\.c%\1%"`

  imp_functions $NAME $TOTAL_FCT

  eval TOTAL_$I=\$NB_OK

  I=`expr $I + 1`
done

$ECHO -e ""
$ECHO -e "   Maximum: $TOTAL_FCT"

$ECHO -e ""
$ECHO -e ""
$ECHO -e "  Supported Audio Outputs:"
$ECHO -e "  ~~~~~~~~~~~~~~~~~~~~~~~~"
$ECHO -e ""

TYPE=AO
PATTERN=".*PLAYER_${TYPE}_\([A-Z0-9_]*\).*"
TOTAL_AO=`grep PLAYER_${TYPE}_ src/player.h | sed "s%$PATTERN%\1%" | sort -u | grep ".*" -c`
I=0
for wrapper in $WRAPPER_LIST;
do
  NAME=`$ECHO -e $wrapper | sed "s%.*_\(.*\)\.c%\1%"`

  sup_output $TYPE $NAME $TOTAL_AO $PATTERN

  eval TOT=\$TOTAL_$I
  TOT=`expr $TOT + $NB_OK`
  eval TOTAL_$I=\$TOT

  I=`expr $I + 1`
done

$ECHO -e ""
$ECHO -e "   Maximum: $TOTAL_AO"

$ECHO -e ""
$ECHO -e ""
$ECHO -e "  Supported Video Outputs:"
$ECHO -e "  ~~~~~~~~~~~~~~~~~~~~~~~~"
$ECHO -e ""

TYPE=VO
PATTERN=".*PLAYER_${TYPE}_\([A-Z0-9_]*\).*"
TOTAL_VO=`grep PLAYER_${TYPE}_ src/player.h | sed "s%$PATTERN%\1%" | sort -u | grep ".*" -c`
I=0
for wrapper in $WRAPPER_LIST;
do
  NAME=`$ECHO -e $wrapper | sed "s%.*_\(.*\)\.c%\1%"`

  sup_output $TYPE $NAME $TOTAL_VO $PATTERN

  eval TOT=\$TOTAL_$I
  TOT=`expr $TOT + $NB_OK`
  eval TOTAL_$I=\$TOT

  I=`expr $I + 1`
done

$ECHO -e ""
$ECHO -e "   Maximum: $TOTAL_VO"

$ECHO -e ""
$ECHO -e ""
$ECHO -e "  Supported Resources:"
$ECHO -e "  ~~~~~~~~~~~~~~~~~~~~"
$ECHO -e ""

PATTERN=".*MRL_RESOURCE_\([A-Z0-9_]*\).*"
TOTAL_RES=`grep MRL_RESOURCE_ src/player.h | sed "s%$PATTERN%\1%" | sort -u | grep ".*" -c`
I=0
for wrapper in $WRAPPER_LIST;
do
  NAME=`$ECHO -e $wrapper | sed "s%.*_\(.*\)\.c%\1%"`

  sup_resource $NAME $TOTAL_RES $PATTERN

  eval TOT=\$TOTAL_$I
  TOT=`expr $TOT + $NB_OK`
  eval TOTAL_$I=\$TOT

  I=`expr $I + 1`
done

$ECHO -e ""
$ECHO -e "   Maximum: $TOTAL_RES"

$ECHO -e ""
$ECHO -e ""
$ECHO -e "  Global statistics (sum):"
$ECHO -e "  ~~~~~~~~~~~~~~~~~~~~~~~~"
$ECHO -e ""

TOTAL_W=0
TOTAL=`expr $TOTAL_FCT + $TOTAL_AO + $TOTAL_VO + $TOTAL_RES`
I=0
for wrapper in $WRAPPER_LIST;
do
  NAME=`$ECHO -e $wrapper | sed "s%.*_\(.*\)\.c%\1%"`
  eval TOTAL_S=\$TOTAL_$I
  TOTAL_W=`expr $TOTAL_W + $TOTAL_S`
  PERCENT=`expr $TOTAL_S \* 100 / $TOTAL`

  bargraph $PERCENT
  $ECHO -e "     $NAME\t: ($TOTAL_S)\t$PERCENT% \t[$BARGRAPH]"

  I=`expr $I + 1`
done

$ECHO -e ""
$ECHO -e "   Maximum: $TOTAL"

PERCENT=`expr $TOTAL_W \* 100 / \( $TOTAL \* \( $I + 1 \) \)`
bargraph $PERCENT
$ECHO -e ""
$ECHO -e ""
$ECHO -e "   libplayer\t:\t$PERCENT%\t[$BARGRAPH]"
$ECHO -e ""