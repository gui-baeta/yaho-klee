#include "network.hpp"

#include "../pch.hpp"
#include "../util/logger.hpp"

#include "device.hpp"
#include "nf.hpp"
#include "link.hpp"

#include "call-paths-to-bdd.h"

namespace Synergio {
	Network::Network(Devices &&devices, NFs &&nfs, Links &&links, BDDs &&bdds): devices(move(devices)), nfs(move(nfs)), links(move(links)), bdds(move(bdds)) {}
	
	Network::~Network() = default;

 	std::unique_ptr<Network> Network::create(Devices &&devices, NFs &&nfs, Links &&links) {
		info("Loading all BDDs");

		BDDs bdds;

		for (auto it = nfs.begin(); it != nfs.end(); ++it) {
			auto& nf = it->second;

			const string &path = nf->get_path();

			if(bdds.find(path) == bdds.end()) {
				bdds.emplace(path, shared_ptr<BDD::BDD>(new BDD::BDD(path)));
				info("BDD loaded for NF ", nf->get_id(), " at ", path);
			}
			else {
				info("BDD already loaded for NF ", nf->get_id(), " at ", path);
			}
		}
		
		success("BDDs loaded");

		return std::unique_ptr<Network>(new Network(move(devices), move(nfs), move(links), move(bdds)));
	}

	void Network::print() {
		debug("Printing Network");

		for (auto it = devices.begin(); it != devices.end(); ++it) {
			auto& device = it->second;
			device->print();
		}

		for (auto it = nfs.begin(); it != nfs.end(); ++it) {
			auto& nf = it->second;
			nf->print();
		}

		for (auto& link : links) {
			link->print();
		}
	}
}
