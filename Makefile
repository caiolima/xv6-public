cscope:
	find -name "*.[chxsS]" -print > cscope.files
	cscope -b -q -k

