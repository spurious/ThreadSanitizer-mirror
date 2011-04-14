CXX=g++
DBGINFO=-g

main:	main.o symbol_table.o
	$(CXX) $^ -o main

%.o:	%.cc %.h
	$(CXX) $(DBGINFO) $< -c

%.o:	%.cc
	$(CXX) $(DBGINFO) $< -c

clean:
	rm main
	rm *.o
