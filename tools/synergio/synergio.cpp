#include "pch.hpp"

#include "parser/parser.hpp"
#include "models/network.hpp"

void usage() {
	cout << "Usage: synergio [input-file]" << endl;
}

int main(int argc, char **argv) {
	if(argc != 2) {
		usage();
		return EXIT_FAILURE;
	}

	const string input_file {argv[1]};
	auto network = Synergio::parse(input_file);

	network->print();
	
	return EXIT_SUCCESS;
}
