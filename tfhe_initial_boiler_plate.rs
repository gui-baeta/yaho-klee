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
use lazy_static::lazy_static;

#[derive(Deserialize)]
pub struct Config {
    pub values: Vec<i32>,
}

lazy_static!{
    pub static ref CONFIG: Config = Config::from_config_file("config.toml").unwrap();
}


type FheUint = FheUint4;

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

	// Split the input by whitespaces and collect them into a vector of strings
	let values: &Vec<i32> = &CONFIG.values;

	set_server_key(server_key);
