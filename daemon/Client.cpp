/* Copyright (C) 2016 Alexander Shishenko <alex@shishenko.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#include "Client.h"
#include "control/Config.h"
#include "control/ControlServer.h"
#include "control/StateCollector.h"
#include "discovery/DiscoveryService.h"
#include "folder/FolderGroup.h"
#include "folder/FolderService.h"
#include "nat/PortMappingService.h"
#include "nodekey/NodeKey.h"
#include "p2p/P2PProvider.h"
#include <util/log.h>

namespace librevault {

Client::Client(int argc, char** argv) : QCoreApplication(argc, argv) {
	setApplicationName("Librevault");
	// Initializing components
	state_collector_ = new StateCollector(this);
	node_key_ = std::make_unique<NodeKey>();
	portmanager_ = std::make_unique<PortMappingService>();
	discovery_ = std::make_unique<DiscoveryService>(*node_key_, *portmanager_, *state_collector_);
	folder_service_ = std::make_unique<FolderService>(*state_collector_);
	p2p_provider_ = std::make_unique<P2PProvider>(*node_key_, *portmanager_, *folder_service_);
	control_server_ = new ControlServer(state_collector_, this);

	/* Connecting signals */
	connect(state_collector_, &StateCollector::globalStateChanged, control_server_, &ControlServer::notify_global_state_changed);
	connect(state_collector_, &StateCollector::folderStateChanged, control_server_, &ControlServer::notify_folder_state_changed);
	discovery_->discovered_node_signal.connect([this](DiscoveryResult node_cred, std::weak_ptr<FolderGroup> group_ptr){
		if(p2p_provider_) p2p_provider_->add_node(node_cred, group_ptr);
	});
	folder_service_->folder_added_signal.connect([this](std::shared_ptr<FolderGroup> group){
		if(discovery_) discovery_->register_group(group);
		if(control_server_) control_server_->notify_folder_added(group->hash(), Config::get()->folder_get(group->hash()));
	});
	folder_service_->folder_removed_signal.connect([this](std::shared_ptr<FolderGroup> group){
		if(discovery_) discovery_->unregister_group(group);
		if(control_server_) control_server_->notify_folder_removed(group->hash());
	});
	connect(control_server_, &ControlServer::restart, this, &Client::restart);
	connect(control_server_, &ControlServer::shutdown, this, &Client::shutdown);
}

Client::~Client() {
	p2p_provider_.reset();
	folder_service_.reset();
	discovery_.reset();
	portmanager_.reset();
	node_key_.reset();
}

int Client::run() {
	discovery_->run();
	folder_service_->run();
	p2p_provider_->run();
	control_server_->run();

	return this->exec();
}

void Client::restart() {
	LOGI("Restarting...");
	this->exit(EXIT_RESTART);
}

void Client::shutdown(){
	LOGI("Exiting...");
	this->exit();
}

} /* namespace librevault */
