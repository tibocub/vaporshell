# an extglob pattern in [[ ]] may span lines, with tabs, in a file read line by line
shopt -s extglob
x=data
if [[ $x == @(kvm|mem|
	|data|help) ]]; then echo B; fi
p=-e q=stat
if [[ $p == @("-e"|"--event") &&
	$q == @(record|stat|top) ]]; then echo A; fi
