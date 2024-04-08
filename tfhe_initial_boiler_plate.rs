use std::io;
use bincode;

use std::fs::File;
use std::io::{Read, Write};
use std::ops::Add;

use tfhe::prelude::*;
use tfhe::{
    generate_keys, set_server_key, ClientKey, ConfigBuilder, FheUint3, FheUint4, ServerKey,
};

use config_file::FromConfigFile;
use serde::Deserialize;

#[derive(Deserialize)]
struct Config {
    values: Vec<u32>,
}

lazy_static!{
    pub static ref CONFIG: Config = Config::from_config_file("config.toml").unwrap();
}


type FheUint = FheUint3;

fn main() {
	let config = ConfigBuilder::all_disabled().enable_default_uint4().build();

	let client_key: ClientKey;
	let server_key: ServerKey;

	// Check if keys were generated (if files exist)
	if File::open("client.key").is_ok() && File::open("server.key").is_ok() {
		println!("Loading keys...");
		// Load keys from files
		client_key = bincode::deserialize(&std::fs::read("client.key").unwrap()).unwrap();
		server_key = bincode::deserialize(&std::fs::read("server.key").unwrap()).unwrap();
	} else {
		println!("Generating keys...");

        (client_key, server_key) = generate_keys(config);

        // Save keys to files
        File::create("client.key")
            .unwrap()
            .write_all(bincode::serialize(&client_key).unwrap().as_slice())
            .unwrap();

        File::create("server.key")
            .unwrap()
            .write_all(bincode::serialize(&server_key).unwrap().as_slice())
            .unwrap();
	}
	println!("Done.");

	// Create a new mutable String to store the user input
	let mut input = String::new();

	// Print a message to prompt the user for input
	println!("Please enter three integers separated by spaces:");

	// Read the user input from stdin
	io::stdin().read_line(&mut input)
	    .expect("Failed to read line");

	// Split the input by whitespaces and collect them into a vector of strings
	let values: Vec<i32> = input.trim()
	    .split_whitespace()
	    .map(|s| s.parse().unwrap()) // Parse each value into an integer
	    .collect();

	set_server_key(server_key);