# Find the version of python on the system.
# If the directory "venv" exists, start the virtual environment.
# Otherwise, install a new virtual environment.
#

OS=$(uname -s | tr '[:upper:]' '[:lower:]')
case "$OS" in
    cygwin*) OS="WINDOWS" ;;
    darwin) OS="MACOS" ;;
    *) OS="LINUX" ;;
esac

if [ -e "/cygdrive/c/python/Python36/python" ]; then
    export SYSTEM_PYTHON="/cygdrive/c/python/Python36/python"
elif [ -e "/opt/mongodbtoolchain/v3/bin/python3" ]; then
    export SYSTEM_PYTHON="/opt/mongodbtoolchain/v3/bin/python3"
elif python3 --version >/dev/null 2>&1; then
    export SYSTEM_PYTHON=python3
else
    export SYSTEM_PYTHON=python
fi

if [ ! -e venv ]; then
    $SYSTEM_PYTHON -m venv ./venv
fi

if [ "$OS" = "WINDOWS" ]; then
    export PYTHON="$(pwd)/venv/Scripts/python"
else
    export PYTHON="$(pwd)/venv/bin/python"
fi