file1="s0/a.txt"
file2="s1/a.txt"
file3="s2/a.txt"
file4="s0/b.txt"
file5="s1/b.txt"
file6="s2/b.txt"
file7="s0/c.txt"
file8="s1/c.txt"
file9="s2/c.txt"
time=0

while true
do
	sleep 3
	rm log/*
	rm s0/*
	rm s1/*
	rm s2/*
	echo "aaa" > s0/a.txt
	echo "aaa" > s1/a.txt
	echo "aaa" > s2/a.txt
	echo "bbb" > s0/b.txt
	echo "bbb" > s1/b.txt
	echo "bbb" > s2/b.txt
	echo "ccc" > s0/c.txt
	echo "ccc" > s1/c.txt
	echo "ccc" > s2/c.txt
    ./D &
	sleep 10
	_pid=`pidof D`
	kill $_pid
	if ! cmp -s "$file1" "$file2"; then
		exit 0
	fi
	if ! cmp -s "$file2" "$file3"; then
		exit 0
	fi
	if ! cmp -s "$file4" "$file5"; then
		exit 0
	fi
	if ! cmp -s "$file5" "$file6"; then
		exit 0
	fi
	if ! cmp -s "$file7" "$file8"; then
		exit 0
	fi
	if ! cmp -s "$file8" "$file9"; then
		exit 0
	fi
	let time++
	echo "OK, test times=$time"
done
