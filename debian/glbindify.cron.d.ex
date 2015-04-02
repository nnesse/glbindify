#
# Regular cron jobs for the glbindify package
#
0 4	* * *	root	[ -x /usr/bin/glbindify_maintenance ] && /usr/bin/glbindify_maintenance
