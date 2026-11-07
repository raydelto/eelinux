# Interactive xterm shells load this file without reading /etc/profile.
case $- in
    *i*) ;;
    *) return ;;
esac
if [ -r /etc/profile.d/bash_completion.sh ]; then
    . /etc/profile.d/bash_completion.sh
fi
