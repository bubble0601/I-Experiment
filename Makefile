phone: phone.c lib.c fft.c
	gcc phone.c -lsox -o phone
read: read_data.c
	gcc read_data.c -o read_data
clean:
	rm phone
