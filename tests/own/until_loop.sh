x=start
until [ "$x" = done ]; do echo until-body; x=done; done
echo after-until
