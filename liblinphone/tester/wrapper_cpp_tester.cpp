/*
 * Copyright (c) 2010-2022 Belledonne Communications SARL.
 *
 * This file is part of Liblinphone
 * (see https://gitlab.linphone.org/BC/public/liblinphone).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <fstream>
#include <iostream>
#include <list>

#include <linphone++/linphone.hh>

#include "bctoolbox/logging.h"

#include "c-wrapper/c-wrapper.h"
#include "c-wrapper/internal/c-tools.h"
#include "liblinphone_tester++.h"
#include "liblinphone_tester.h"
#include "linphone/api/c-chat-room.h"
#include "linphone/core.h"
#include "linphone/wrapper_utils.h"
#include "local-conference-tester-functions.h"
#include "mediastreamer2/msogl.h"
#include "payload-type/payload-type.h"
#include "tester_utils.h"

//--------------------------------------------------------------------------------------------
//					Class handlers
//--------------------------------------------------------------------------------------------
class AutoAcceptHandler : public linphone::CoreListener {
public:
	virtual void onCallStateChanged(const std::shared_ptr<linphone::Core> &,
	                                const std::shared_ptr<linphone::Call> &call,
	                                linphone::Call::State state,
	                                const std::string &message) override {
		lDebug() << "[AutoAcceptHandler] Call state changed : (" << (int)state << ") " << message;
		if (state == linphone::Call::State::IncomingReceived) call->accept();
	}
};

//--------------------------------------------------------------------------------------------

static void create_account() {
	LinphoneCoreManager *marie = linphone_core_manager_new("marie_rc");

	// Get C++ and start working from it.
	auto core = linphone::Object::cPtrToSharedPtr<linphone::Core>(marie->lc, TRUE);

	auto accountCreator = core->createAccountCreator("");
	accountCreator->setUsername("toto");
	accountCreator->setDomain("sip.example.org");
	auto account = accountCreator->createAccountInCore();
	account = nullptr; // Clean account

	core = nullptr; // C++ Core deletion
	wait_for_until(marie->lc, NULL, NULL, 0, 500);

	// C clean
	linphone_core_manager_destroy(marie);
}

static void account_freed_after_core_destroyed() {
	LinphoneCoreManager *marie = linphone_core_manager_new("marie_rc");

	// Get C++ and start working from it.
	auto core = linphone::Object::cPtrToSharedPtr<linphone::Core>(marie->lc, TRUE);

	auto account = core->getDefaultAccount();
	auto accountParams = account->getParams()->clone();
	accountParams->enableRegister(false);
	account->setParams(accountParams);
	core->clearAccounts();
	core = nullptr; // C++ Core deletion
	// C clean
	linphone_core_manager_destroy(marie);

	// Release last reference after the core is destroyed
	account = nullptr;
}

static void create_chat_room() {
	// Init from C
	LinphoneCoreManager *marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager *pauline = linphone_core_manager_new("pauline_tcp_rc");

	// Get C++ and start working from it.
	auto core = linphone::Object::cPtrToSharedPtr<linphone::Core>(marie->lc, TRUE);

	// Chat room parameters
	auto params = core->createDefaultChatRoomParams();
	std::list<std::shared_ptr<linphone::Address>> participants;
	std::shared_ptr<const linphone::Address> localAddress;
	participants.push_back(linphone::Object::cPtrToSharedPtr<linphone::Address>(pauline->identity));
	params->setBackend(linphone::ChatRoom::Backend::Basic);

	// Creation, store the result inside a variable to test variable scope.
	auto chatRoom = core->createChatRoom(params, localAddress, participants);

	auto cChatRoom = (LinphoneChatRoom *)linphone::Object::sharedPtrToCPtr(chatRoom);
	linphone_chat_room_ref(cChatRoom);
	linphone_chat_room_unref(cChatRoom); // Should not delete chat room. Refs are : Core + chatRoom

	auto chatRooms = core->getChatRooms();
	BC_ASSERT_EQUAL((int)chatRooms.size(), 1, int, "%d");
	auto cr = chatRooms.front(); // Use only one item.
	chatRooms.clear();

	auto cCr = (LinphoneChatRoom *)linphone::Object::sharedPtrToCPtr(cr);
	linphone_chat_room_ref(cCr);
	linphone_chat_room_unref(cCr); // Refs are : Core + chatRoom + cr
	cr = nullptr;                  // Refs are : Core + chatRoom

	wait_for_until(marie->lc, pauline->lc, NULL, 0, 300);
	core->deleteChatRoom(chatRoom);
	wait_for_until(marie->lc, pauline->lc, NULL, 0, 500);

	cChatRoom = (LinphoneChatRoom *)linphone::Object::sharedPtrToCPtr(chatRoom);
	linphone_chat_room_ref(cChatRoom);
	linphone_chat_room_unref(cChatRoom); // Ref is : chatRoom

	chatRoom = nullptr; // Delete chat room.

	core = nullptr; // C++ Core deletion
	wait_for_until(marie->lc, pauline->lc, NULL, 0, 500);

	// C clean
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static void create_conference() {
	std::shared_ptr<AutoAcceptHandler> handler = std::make_shared<AutoAcceptHandler>();
	// Init from C
	LinphoneCoreManager *marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager *pauline = linphone_core_manager_new("pauline_tcp_rc");

	// Get C++ and start working from it.
	auto marieCore = linphone::Object::cPtrToSharedPtr<linphone::Core>(marie->lc, TRUE);
	auto paulineCore = linphone::Object::cPtrToSharedPtr<linphone::Core>(pauline->lc, TRUE);

	paulineCore->addListener(handler);

	auto confParams = marieCore->createConferenceParams(nullptr);
	BC_ASSERT_PTR_NOT_NULL(confParams);
	confParams->enableVideo(false);
	confParams->enableLocalParticipant(true);
	confParams->enableChat(false);

	auto conference = marieCore->createConferenceWithParams(confParams);
	BC_ASSERT_PTR_NOT_NULL(conference);

	std::list<std::shared_ptr<linphone::Address>> participants;

	participants.push_back(
	    marieCore->createAddress(linphone::Object::cPtrToSharedPtr<linphone::Address>(pauline->identity)->asString()));

	auto callParams = marieCore->createCallParams(nullptr);
	callParams->enableVideo(false);
	callParams->setAudioDirection(linphone::MediaDirection::SendRecv);

	conference->inviteParticipants(participants, callParams);

	wait_for_until(marie->lc, pauline->lc, NULL, 0, 1000);

	if (marieCore->getCurrentCall()) marieCore->getCurrentCall()->terminate();
	if (paulineCore->getCurrentCall()) paulineCore->getCurrentCall()->terminate();
	conference->terminate();
	wait_for_until(marie->lc, pauline->lc, NULL, 0, 500);

	callParams = nullptr;
	participants.clear();
	conference = nullptr;
	confParams = nullptr;

	marieCore->stop();
	wait_for_until(marie->lc, pauline->lc, NULL, 0, 100);
	marieCore->start();
	wait_for_until(marie->lc, pauline->lc, NULL, 0, 100);

	paulineCore = nullptr; // C++ Core deletion
	marieCore = nullptr;
	wait_for_until(marie->lc, pauline->lc, NULL, 0, 500);

	// C clean
	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(marie);
}

static void various_api_checks(void) {
	LinphoneCoreManager *marie = linphone_core_manager_new("marie_rc");

	auto core = linphone::Object::cPtrToSharedPtr<linphone::Core>(marie->lc, TRUE);

	/* check a few accessors */
	auto defaultAccount = core->getDefaultAccount();
	BC_ASSERT_PTR_NOT_NULL(defaultAccount);
	auto contactAddress = defaultAccount->getContactAddress();
	BC_ASSERT_PTR_NOT_NULL(contactAddress);
	auto newAddress = contactAddress->clone();
	BC_ASSERT_PTR_NOT_NULL(newAddress);

	std::list<std::shared_ptr<linphone::Address>> participants;
	auto address = linphone::Factory::get()->createAddress("sip:toto@sip.linphone.org");
	participants.push_back(address);
	auto conferenceInfo = linphone::Factory::get()->createConferenceInfo();
	conferenceInfo->setParticipants(participants);
	auto testList = conferenceInfo->getParticipants();
	BC_ASSERT_EQUAL(participants.size(), testList.size(), size_t, "%zu");

	linphone_core_manager_destroy(marie);
}

static void displaying_payload_type(void) {
	// Init from C
	LinphoneCoreManager *marie = linphone_core_manager_new("marie_rc");
	// Get C++ and start working from it.
	auto core = linphone::Object::cPtrToSharedPtr<linphone::Core>(marie->lc, TRUE);

	auto payloads = core->getAudioPayloadTypes();
	for (auto it : payloads) {
		lInfo() << "Get mime type : " << it->getMimeType();
		lInfo() << "Encoder description : " << it->getEncoderDescription();
	}

	payloads.clear();

	core = nullptr; // C++ Core deletion

	// C clean
	linphone_core_manager_destroy(marie);
}
using namespace LinphoneTest;
class WrapperTools {
public:
	static std::list<LinphoneTest::ClientConference *> createClients(LinphoneConferenceSecurityLevel security_level,
	                                                                 const std::list<std::string> &clients,
	                                                                 const Address &focusConferenceFactory

	) {
		std::list<LinphoneTest::ClientConference *> clientConference;
		bool_t encrypted_conference = (security_level == LinphoneConferenceSecurityLevelEndToEnd ? TRUE : FALSE);
		const LinphoneTesterLimeAlgo lime_algo = encrypted_conference ? C25519 : UNSET;
		for (auto client : clients) {
			clientConference.push_back(new LinphoneTest::ClientConference(client, focusConferenceFactory, lime_algo));
		}
		return clientConference;
	}
	static void deleteClients(std::list<LinphoneTest::ClientConference *> clients) {
		for (auto c : clients)
			delete c;
	}
	static void activateVideo(std::shared_ptr<linphone::Core> core) {
		core->setVideoDisplayFilter("MSOGL");
		core->usePreviewWindow(true);
		core->enableVideoPreview(true);
		core->enableVideoDisplay(true);
	}
	static void setWindowId(std::shared_ptr<linphone::Core> core,
	                        MSOglContextInfo &context,
	                        void **windowId,
	                        const std::string &title,
	                        bool preview) {
		if (!*windowId)
			*windowId =
			    preview ? core->createNativePreviewWindowId(&context) : core->createNativeVideoWindowId(&context);
		setWindowTitle(*windowId, title.c_str());
		auto call = core->getCurrentCall();
		if (preview) core->setNativePreviewWindowId(*windowId);
		else if (call) call->setNativeVideoWindowId(*windowId);
		else core->setNativeVideoWindowId(*windowId);
	}
	/*
static
void createConferenceWithScreenSharing(Focus &focus,
	                std::list<LinphoneTest::ClientConference*> confParticipants,
	                time_t start_time,
	                                           int duration,
	                                            const LinphoneMediaEncryption encryption,
	                                            LinphoneConferenceLayout layout,
	                                            bool_t enable_video,
	                                            BCTBX_UNUSED(bool_t enable_camera),
	                                            BCTBX_UNUSED(bool_t turn_off_screen_sharing),
	                                            BCTBX_UNUSED(LinphoneMediaDirection video_direction),
	                                            LinphoneConferenceSecurityLevel security_level,
	                                            std::list<LinphoneParticipantRole> allowedRoles) {

	    {
	        // to make sure focus is destroyed after clients.
	        bool_t encrypted_conference = (security_level == LinphoneConferenceSecurityLevelEndToEnd ? TRUE : FALSE);
	        const LinphoneTesterLimeAlgo lime_algo = encrypted_conference ? C25519 : UNSET;


	        for (auto c : confParticipants)
	        focus.registerAsParticipantDevice(*c);

	        //int nb_subscriptions = 1;
	        //if (security_level == LinphoneConferenceSecurityLevelEndToEnd) {
	        //	nb_subscriptions = 2; // One more subscription for the EKT
	        //}

	        setup_conference_info_cbs(marie.getCMgr());

	        bctbx_list_t *coresList = NULL;

	        for (auto mgr : {focus.getCMgr(), marie.getCMgr(), pauline.getCMgr(), laure.getCMgr(), michelle.getCMgr(),
	                         berthe.getCMgr()}) {
	            if (enable_video) {
	                LinphoneVideoActivationPolicy *pol =
	                    linphone_factory_create_video_activation_policy(linphone_factory_get());
	                linphone_video_activation_policy_set_automatically_accept(pol, TRUE);
	                linphone_video_activation_policy_set_automatically_initiate(pol, TRUE);
	                linphone_core_set_video_activation_policy(mgr->lc, pol);
	                linphone_video_activation_policy_unref(pol);

	                linphone_core_set_video_device(mgr->lc, liblinphone_tester_mire_id);
	                linphone_core_enable_video_capture(mgr->lc, TRUE);
	                linphone_core_enable_video_display(mgr->lc, TRUE);

	                if (layout == LinphoneConferenceLayoutGrid) {
	                    linphone_core_set_preferred_video_definition_by_name(mgr->lc, "720p");
	                    linphone_config_set_string(linphone_core_get_config(mgr->lc), "video", "max_conference_size",
	                                               "vga");
	                }

	                if (mgr == pauline.getCMgr()) {
	                    linphone_core_set_conference_max_thumbnails(mgr->lc, 3);
	                }
	            }

	            if (mgr != focus.getCMgr()) {
	                linphone_core_set_default_conference_layout(mgr->lc, layout);
	                linphone_core_set_media_encryption(mgr->lc, encryption);
	            }

	            // Enable ICE at the account level but not at the core level
	            enable_stun_in_mgr(mgr, TRUE, TRUE, FALSE, FALSE);

	            linphone_config_set_int(linphone_core_get_config(mgr->lc), "sip", "update_call_when_ice_completed",
TRUE); linphone_config_set_int(linphone_core_get_config(mgr->lc), "sip", "update_call_when_ice_completed_with_dtls",
FALSE);

	            coresList = bctbx_list_append(coresList, mgr->lc);
	                         }

	        if (encrypted_conference) {
	            configure_end_to_end_encrypted_conference_server(focus);
	            BC_ASSERT_TRUE(linphone_core_lime_x3dh_enabled(marie.getLc()));
	            BC_ASSERT_TRUE(linphone_core_lime_x3dh_enabled(pauline.getLc()));
	            BC_ASSERT_TRUE(linphone_core_lime_x3dh_enabled(laure.getLc()));
	            BC_ASSERT_TRUE(linphone_core_lime_x3dh_enabled(michelle.getLc()));
	            BC_ASSERT_TRUE(linphone_core_lime_x3dh_enabled(berthe.getLc()));
	        }

	        int nortp_timeout = 10;
	        linphone_core_set_nortp_timeout(marie.getLc(), nortp_timeout);
	        linphone_core_set_file_transfer_server(marie.getLc(), file_transfer_url);
	        linphone_core_set_conference_participant_list_type(focus.getLc(),
LinphoneConferenceParticipantListTypeClosed);

	        //stats focus_stat = focus.getStats();

	        //const bool oneRoleAllowed = (allowedRoles.size() == 1);
	        bool speakerAllowed = std::find(allowedRoles.cbegin(), allowedRoles.cend(), LinphoneParticipantRoleSpeaker)
!= allowedRoles.cend(); bool listenerAllowed = std::find(allowedRoles.cbegin(), allowedRoles.cend(),
LinphoneParticipantRoleListener) != allowedRoles.cend();
	        //bool all_listeners = listenerAllowed && oneRoleAllowed;

	        std::list<LinphoneCoreManager *> participants{laure.getCMgr(), pauline.getCMgr(), michelle.getCMgr(),
	                                                      berthe.getCMgr()};
	        std::list<LinphoneCoreManager *> conferenceMgrs{focus.getCMgr(), marie.getCMgr(),    pauline.getCMgr(),
	                                                        laure.getCMgr(), michelle.getCMgr(), berthe.getCMgr()};
	        std::list<LinphoneCoreManager *> members{marie.getCMgr(), pauline.getCMgr(), laure.getCMgr(),
	                                                 michelle.getCMgr(), berthe.

	        time_t end_time = (duration <= 0) ? -1 : (start_time + duration * 60);
	        const char *initialSubject = "Test characters: ^ :) ¤ çà @";
	        const char *description = "Paris Baker";

	        bctbx_list_t *participants_info = NULL;
	        std::map<LinphoneCoreManager *, LinphoneParticipantInfo *> participantList;
	        participantList.insert(std::make_pair(
	            laure.getCMgr(), add_participant_info_to_list(&participants_info, laure.getCMgr()->identity,
	                                                          (listenerAllowed) ? LinphoneParticipantRoleListener
	                                                                            : LinphoneParticipantRoleSpeaker,
	                                                          -1)));
	        participantList.insert(std::make_pair(
	            pauline.getCMgr(), add_participant_info_to_list(&participants_info, pauline.getCMgr()->identity,
	                                                            LinphoneParticipantRoleSpeaker, -1)));
	        participantList.insert(std::make_pair(
	            michelle.getCMgr(), add_participant_info_to_list(&participants_info, michelle.getCMgr()->identity,
	                                                             (listenerAllowed) ? LinphoneParticipantRoleListener
	                                                                               : LinphoneParticipantRoleSpeaker,
	                                                             -1)));
	        participantList.insert(std::make_pair(
	            berthe.getCMgr(), add_participant_info_to_list(&participants_info, berthe.getCMgr()->identity,
	                                                           (speakerAllowed) ? LinphoneParticipantRoleSpeaker
	                                                                            : LinphoneParticipantRoleListener,
	                                                           -1)));

	        LinphoneAddress *confAddr =
	            create_conference_on_server(focus, marie, participantList, start_time, end_time, initialSubject,
	                                        description, TRUE, security_level, TRUE, FALSE, NULL);
	        BC_ASSERT_PTR_NOT_NULL(confAddr);
	        char *conference_address_str = (confAddr) ? linphone_address_as_string(confAddr) : ms_strdup("sip:");

	        // Chat room creation to send ICS
	        BC_ASSERT_TRUE(wait_for_list(coresList, &marie.getStats().number_of_LinphoneChatRoomStateCreated, 4,
	                                     liblinphone_tester_sip_timeout));
	        BC_ASSERT_PTR_NOT_NULL(conference_address_str);
	    }
	}
*/
	static bctbx_list_t *getCoreList(LinphoneTest::Focus &focus,
	                                 std::list<LinphoneTest::ClientConference *> participants) {
		bctbx_list_t *coresList = NULL;
		coresList = bctbx_list_append(coresList, &focus.getCCore());
		for (auto p : participants)
			coresList = bctbx_list_append(coresList, &p->getCCore());
		return coresList;
	}
	static std::list<std::reference_wrapper<CoreManager>>
	getCoreManagerList(LinphoneTest::Focus &focus, std::list<LinphoneTest::ClientConference *> confParticipants) {
		std::list<std::reference_wrapper<CoreManager>> allCores;
		allCores.push_back(focus);
		for (auto client : confParticipants) {
			allCores.push_back(*client);
		}
		return allCores;
	}

	static std::list<stats> getStats(const std::list<std::reference_wrapper<CoreManager>> &coreManagers) {
		std::list<stats> allStats;
		for (auto c : coreManagers) {
			allStats.push_back(c.get().getStats());
		}
		return allStats;
	}

	static LinphoneAddress *
	createConferenceWithScreenSharing(LinphoneTest::Focus &focus,
	                                  std::list<LinphoneTest::ClientConference *> confParticipants,

	                                  time_t start_time,
	                                  int duration,
	                                  const LinphoneMediaEncryption encryption,
	                                  LinphoneConferenceLayout layout,
	                                  bool_t enable_video,
	                                  LinphoneMediaDirection video_direction,
	                                  LinphoneConferenceSecurityLevel security_level,
	                                  std::list<LinphoneParticipantRole> allowedRoles) {
		std::list<std::reference_wrapper<CoreManager>> allCores;
		auto itMainClientConf = confParticipants.begin();
		auto itSecondClientConf = confParticipants.begin();
		++itSecondClientConf;

		// AllCore => Focus ; Main ; Second ; Aux
		allCores.push_back(focus);
		for (auto client : confParticipants) {
			focus.registerAsParticipantDevice(*client);
			allCores.push_back(*client);
		}
		bool_t encrypted_conference = (security_level == LinphoneConferenceSecurityLevelEndToEnd ? TRUE : FALSE);

		setup_conference_info_cbs((*confParticipants.begin())->getCMgr());

		int coreCount = 0;
		for (auto core : allCores) {
			auto mgr = &core.get();
			LinphoneCore *mgrCore = &mgr->getCCore();
			if (enable_video) {
				LinphoneVideoActivationPolicy *pol =
				    linphone_factory_create_video_activation_policy(linphone_factory_get());
				linphone_video_activation_policy_set_automatically_accept(pol, TRUE);
				linphone_video_activation_policy_set_automatically_initiate(pol, TRUE);
				linphone_core_set_video_activation_policy(mgrCore, pol);
				linphone_video_activation_policy_unref(pol);

				linphone_core_set_video_device(mgrCore, liblinphone_tester_mire_id);
				linphone_core_enable_video_capture(mgrCore, TRUE);
				linphone_core_enable_video_display(mgrCore, TRUE);

				if (layout == LinphoneConferenceLayoutGrid) {
					linphone_core_set_preferred_video_definition_by_name(mgrCore, "720p");
					linphone_config_set_string(linphone_core_get_config(mgrCore), "video", "max_conference_size",
					                           "vga");
				}

				if (coreCount == 2) { // Second
					linphone_core_set_conference_max_thumbnails(mgrCore, 3);
				}
			}

			// not Focus
			if (coreCount > 0) {
				linphone_core_set_default_conference_layout(mgrCore, layout);
				linphone_core_set_media_encryption(mgrCore, encryption);
			}

			// Enable ICE at the account level but not at the core level
			enable_stun_in_mgr(mgr->getCMgr(), TRUE, TRUE, FALSE, FALSE);

			linphone_config_set_int(linphone_core_get_config(mgrCore), "sip", "update_call_when_ice_completed", TRUE);
			linphone_config_set_int(linphone_core_get_config(mgrCore), "sip",
			                        "update_call_when_ice_completed_with_dtls", FALSE);
			++coreCount;
		}
		bctbx_list_t *coresList = getCoreList(focus, confParticipants);
		if (encrypted_conference) {
			configure_end_to_end_encrypted_conference_server(focus);
			for (auto client : confParticipants)
				BC_ASSERT_TRUE(linphone_core_lime_x3dh_enabled(client->getLc()));
		}

		int nortp_timeout = 10;
		linphone_core_set_nortp_timeout((*itMainClientConf)->getLc(), nortp_timeout);
		linphone_core_set_file_transfer_server((*itMainClientConf)->getLc(), file_transfer_url);
		linphone_core_set_conference_participant_list_type(focus.getLc(), LinphoneConferenceParticipantListTypeClosed);

		stats focus_stat = focus.getStats();

		bool speakerAllowed = std::find(allowedRoles.cbegin(), allowedRoles.cend(), LinphoneParticipantRoleSpeaker) !=
		                      allowedRoles.cend();
		bool listenerAllowed = std::find(allowedRoles.cbegin(), allowedRoles.cend(), LinphoneParticipantRoleListener) !=
		                       allowedRoles.cend();

		std::list<LinphoneCoreManager *> participants;   // All except first and second
		std::list<LinphoneCoreManager *> conferenceMgrs; // All
		std::list<LinphoneCoreManager *> members;        // All except first(focus)
		std::map<LinphoneCoreManager *, LinphoneParticipantInfo *> participantList;
		bctbx_list_t *participants_info = NULL;
		conferenceMgrs.push_back(focus.getCMgr());
		coreCount = 0;
		for (auto participant : confParticipants) {
			if (coreCount > 0) {
				participants.push_back(participant->getCMgr());
				LinphoneParticipantRole role;
				if (coreCount == 1 || (!listenerAllowed && coreCount != 4) || (speakerAllowed && coreCount == 4))
					role = LinphoneParticipantRoleSpeaker;
				else role = LinphoneParticipantRoleListener;

				participantList.insert(std::make_pair(
				    participant->getCMgr(),
				    add_participant_info_to_list(&participants_info, participant->getCMgr()->identity, role, -1)));
			}
			conferenceMgrs.push_back(participant->getCMgr());
			members.push_back(participant->getCMgr());
			++coreCount;
		}

		time_t end_time = (duration <= 0) ? -1 : (start_time + duration * 60);
		const char *initialSubject = "Test characters: ^ :) ¤ çà @";
		const char *description = "Paris Baker";

		LinphoneAddress *confAddr =
		    create_conference_on_server(focus, *(*itMainClientConf), participantList, start_time, end_time,
		                                initialSubject, description, TRUE, security_level, TRUE, FALSE, NULL);
		BC_ASSERT_PTR_NOT_NULL(confAddr);
		char *conference_address_str = (confAddr) ? linphone_address_as_string(confAddr) : ms_strdup("sip:");

		// Chat room creation to send ICS
		BC_ASSERT_TRUE(wait_for_list(coresList, &(*itMainClientConf)->getStats().number_of_LinphoneChatRoomStateCreated,
		                             4, liblinphone_tester_sip_timeout));

		std::list<std::pair<LinphoneCoreManager *, stats>> member_stats_list;
		coreCount = 0;
		for (auto mgr : members) {
			member_stats_list.push_back(std::make_pair(mgr, mgr->stat));
			LinphoneCallParams *new_params = linphone_core_create_call_params(mgr->lc, nullptr);
			linphone_call_params_set_media_encryption(new_params, encryption);
			linphone_call_params_set_video_direction(new_params, video_direction);
			if (coreCount == 1) {
				linphone_call_params_enable_mic(new_params, FALSE);
			}
			std::string file = "record-" + std::string(linphone_address_get_username(mgr->identity)) + ".mkv";
			linphone_call_params_set_record_file(new_params, file.c_str());

			ms_message("%s is entering conference %s", linphone_core_get_identity(mgr->lc), conference_address_str);
			linphone_core_invite_address_with_params_2(mgr->lc, confAddr, new_params, nullptr, nullptr);
			linphone_call_params_unref(new_params);
			LinphoneCall *participant_call = linphone_core_get_call_by_remote_address2(mgr->lc, confAddr);
			BC_ASSERT_PTR_NOT_NULL(participant_call);
			if (participant_call) {
				LinphoneCallLog *call_log = linphone_call_get_call_log(participant_call);
				BC_ASSERT_TRUE(linphone_call_log_was_conference(call_log));
			}
			++coreCount;
		}

		check_call_establishment(allCores, members, std::make_pair(focus.getCMgr(), focus_stat), member_stats_list,
		                         (*itMainClientConf)->getCMgr(), confAddr, security_level, encryption, FALSE,
		                         (start_time < 0), TRUE);
		std::map<LinphoneCoreManager *, LinphoneParticipantInfo *> memberList =
		    fill_member_list(members, participantList, (*itMainClientConf)->getCMgr(), participants_info);
		wait_for_conference_streams(allCores, conferenceMgrs, focus.getCMgr(), memberList, confAddr, enable_video,
		                            security_level);

		for (const auto &mgr : conferenceMgrs) {
			BC_ASSERT_TRUE(CoreManagerAssert(allCores).waitUntil(
			    chrono::seconds(50), [mgr, enable_video, video_direction, confAddr] {
				    bool_t video_ok = TRUE;
				    LinphoneConference *conference = linphone_core_search_conference_2(mgr->lc, confAddr);
				    bctbx_list_t *devices = linphone_conference_get_participant_device_list(conference);
				    for (bctbx_list_t *itd = devices; itd; itd = bctbx_list_next(itd)) {
					    LinphoneParticipantDevice *device = (LinphoneParticipantDevice *)bctbx_list_get_data(itd);
					    const LinphoneAddress *device_address = linphone_participant_device_get_address(device);
					    LinphoneParticipant *participant =
					        linphone_conference_is_me(conference, device_address)
					            ? linphone_conference_get_me(conference)
					            : linphone_conference_find_participant(conference, device_address);
					    if (participant) {
						    bool_t video_available =
						        linphone_participant_device_get_stream_availability(device, LinphoneStreamTypeVideo);
						    if (enable_video &&
						        ((video_direction == LinphoneMediaDirectionSendOnly) ||
						         (video_direction == LinphoneMediaDirectionSendRecv)) &&
						        (linphone_participant_get_role(participant) == LinphoneParticipantRoleSpeaker)) {
							    video_ok &= video_available;
						    } else {
							    video_ok &= !video_available;
						    }
					    } else {
						    video_ok = false;
					    }
				    }
				    if (devices) {
					    bctbx_list_free_with_data(devices, (bctbx_list_free_func)linphone_participant_device_unref);
				    }
				    return video_ok;
			    }));
		}

		ms_free(conference_address_str);
		bctbx_list_free_with_data(participants_info, (bctbx_list_free_func)linphone_participant_info_unref);
		bctbx_list_free(coresList);
		// linphone_address_unref(confAddr);
		return confAddr;
	}
	static void activateScreenSharing(LinphoneTest::Focus &focus,
	                                  std::list<LinphoneTest::ClientConference *> participants,
	                                  const LinphoneTest::ClientConference *toActivate,
	                                  LinphoneAddress *confAddr,

	                                  // time_t start_time,
	                                  // int duration,
	                                  // const LinphoneMediaEncryption encryption,
	                                  // LinphoneConferenceLayout layout,
	                                  bool_t enable_video,
	                                  bool_t enable_camera,
	                                  LinphoneMediaDirection video_direction,
	                                  // LinphoneConferenceSecurityLevel security_level,
	                                  std::list<LinphoneParticipantRole> allowedRoles) {
		auto itActivation = std::find(participants.begin(), participants.end(), toActivate);
		if (itActivation == participants.end()) return;
		LinphoneCore *coreActivation = &(*itActivation)->getCCore();
		const bool oneRoleAllowed = (allowedRoles.size() == 1);
		// bool speakerAllowed = std::find(allowedRoles.cbegin(), allowedRoles.cend(), LinphoneParticipantRoleSpeaker)
		// != 					  allowedRoles.cend();
		bool listenerAllowed = std::find(allowedRoles.cbegin(), allowedRoles.cend(), LinphoneParticipantRoleListener) !=
		                       allowedRoles.cend();
		bool all_listeners = listenerAllowed && oneRoleAllowed;
		bool_t clients_have_video_send_component = ((video_direction == LinphoneMediaDirectionSendOnly) ||
		                                            (video_direction == LinphoneMediaDirectionSendRecv));
		bool_t can_screen_share = (enable_video && !all_listeners && clients_have_video_send_component);

		auto allCores = getCoreManagerList(focus, participants);
		bctbx_list_t *coresList = getCoreList(focus, participants);
		std::list<LinphoneCoreManager *> members; // All except first(focus)
		for (auto p : participants)
			members.push_back(p->getCMgr());

		// Berthe enables screen sharing
		ms_message("Test %s enables screen sharing", linphone_core_get_identity(coreActivation));
		LinphoneCall *call = linphone_core_get_call_by_remote_address2(coreActivation, confAddr);
		BC_ASSERT_PTR_NOT_NULL(call);
		if (call) {
			LinphoneCallParams *new_params = linphone_core_create_call_params(coreActivation, call);
			linphone_call_params_enable_video(new_params, TRUE);
			linphone_call_params_enable_camera(new_params, enable_camera);
			linphone_call_params_enable_screen_sharing(new_params, TRUE);
			toggle_screen_sharing(allCores, focus.getCMgr(), members, (*itActivation)->getCMgr(), new_params, confAddr,
			                      can_screen_share);
			linphone_call_params_unref(new_params);
		}
		if (can_screen_share) {
			if (!enable_camera) {
				auto oldStats = getStats(allCores);
				auto itOldStats = oldStats.begin();
				for (auto core : allCores) {
					if (&core.get() == *itActivation) {
						++itOldStats;
						continue;
					}

					if (&core.get() == &focus) {
						BC_ASSERT_TRUE(wait_for_list(
						    coresList, &core.get().getStats().number_of_LinphoneCallUpdatedByRemote,
						    itOldStats->number_of_LinphoneCallUpdatedByRemote + 4, liblinphone_tester_sip_timeout));
						BC_ASSERT_TRUE(wait_for_list(
						    coresList, &core.get().getStats().number_of_LinphoneCallStreamsRunning,
						    itOldStats->number_of_LinphoneCallStreamsRunning + 4, liblinphone_tester_sip_timeout));
					} else {
						BC_ASSERT_TRUE(wait_for_list(coresList, &core.get().getStats().number_of_LinphoneCallUpdating,
						                             itOldStats->number_of_LinphoneCallUpdating + 1,
						                             liblinphone_tester_sip_timeout));
						BC_ASSERT_TRUE(wait_for_list(
						    coresList, &core.get().getStats().number_of_LinphoneCallStreamsRunning,
						    itOldStats->number_of_LinphoneCallStreamsRunning + 1, liblinphone_tester_sip_timeout));
					}
					++itOldStats;
				}
			}

			if (call) {
				const LinphoneCallParams *call_cparams = linphone_call_get_current_params(call);
				BC_ASSERT_EQUAL(linphone_call_params_video_enabled(call_cparams), 1, int, "%0d");
				BC_ASSERT_EQUAL(linphone_call_params_screen_sharing_enabled(call_cparams), 1, int, "%0d");
			}

			LinphoneCall *focus_call =
			    linphone_core_get_call_by_remote_address2(focus.getLc(), (*itActivation)->getCMgr()->identity);
			BC_ASSERT_PTR_NOT_NULL(focus_call);
			if (focus_call) {
				const LinphoneCallParams *call_cparams = linphone_call_get_current_params(focus_call);
				BC_ASSERT_EQUAL(linphone_call_params_video_enabled(call_cparams), 1, int, "%0d");
				BC_ASSERT_EQUAL(linphone_call_params_screen_sharing_enabled(call_cparams), 1, int, "%0d");

				const LinphoneCallParams *call_rparams = linphone_call_get_remote_params(focus_call);
				BC_ASSERT_EQUAL(linphone_call_params_video_enabled(call_rparams), 1, int, "%0d");
				BC_ASSERT_EQUAL(linphone_call_params_screen_sharing_enabled(call_rparams), 1, int, "%0d");
				BC_ASSERT_EQUAL(linphone_call_params_camera_enabled(call_rparams), enable_camera, int, "%0d");
			}
		} else {
			if (call) {
				const LinphoneCallParams *call_cparams = linphone_call_get_current_params(call);
				BC_ASSERT_EQUAL(linphone_call_params_video_enabled(call_cparams),
				                enable_video && clients_have_video_send_component, int, "%0d");
				BC_ASSERT_EQUAL(linphone_call_params_screen_sharing_enabled(call_cparams), 0, int, "%0d");
			}

			for (const auto &coreManager : allCores) {
				auto mgr = coreManager.get().getCMgr();
				LinphoneConference *conference = linphone_core_search_conference_2(mgr->lc, confAddr);
				BC_ASSERT_PTR_NULL(linphone_conference_get_screen_sharing_participant_device(conference));
				BC_ASSERT_PTR_NULL(linphone_conference_get_screen_sharing_participant(conference));
				bctbx_list_t *devices = linphone_conference_get_participant_device_list(conference);
				for (bctbx_list_t *itd = devices; itd; itd = bctbx_list_next(itd)) {
					LinphoneParticipantDevice *device = (LinphoneParticipantDevice *)bctbx_list_get_data(itd);
					BC_ASSERT_FALSE(linphone_participant_device_screen_sharing_enabled(device));
				}
				if (devices) {
					bctbx_list_free_with_data(devices, (bctbx_list_free_func)linphone_participant_device_unref);
				}
			}
		}
	}
};

uintptr_t getDisplayIndex(BCTBX_UNUSED(void *screenSharing)) {
#ifdef ENABLE_SCREENSHARING
	return *(uintptr_t *)(&screenSharing);
#else
	return 0;
#endif
}

static void screen_sharing(void) {
	// std::shared_ptr<LinphoneTest::Focus> focus = std::make_shared<LinphoneTest::Focus>("chloe_rc");
	Focus focus("chloe_rc");
	auto clientConferences = WrapperTools::createClients(
	    LinphoneConferenceSecurityLevelNone, {"marie_rc", "pauline_rc", "laure_tcp_rc", "michelle_rc", "berthe_rc"},
	    focus.getConferenceFactoryAddress());
	auto screenSharerCore = *clientConferences.rbegin();
	auto itParticipants = clientConferences.begin();
	auto marieCore = *itParticipants;
	++itParticipants;
	auto paulineCore = *itParticipants;
	bctbx_list_t *coresList = WrapperTools::getCoreList(focus, clientConferences);
	auto screenSharer = linphone::Object::cPtrToSharedPtr<linphone::Core>(&screenSharerCore->getCCore(), TRUE);
	auto marie = linphone::Object::cPtrToSharedPtr<linphone::Core>(&marieCore->getCCore(), TRUE);
	auto pauline = linphone::Object::cPtrToSharedPtr<linphone::Core>(&paulineCore->getCCore(), TRUE);

	disable_all_video_codecs_except_one(&focus.getCCore(), "VP8");
	for (auto p : clientConferences)
		disable_all_video_codecs_except_one(&p->getCCore(), "VP8");
	// marie->setRecordFile("record-marie.mkv");
	// pauline->setRecordFile("record-pauline.mkv");

	//	create_conference_with_screen_sharing_base(ms_time(NULL), -1, LinphoneMediaEncryptionNone,
	//											   LinphoneConferenceLayoutActiveSpeaker, TRUE, TRUE, FALSE,
	//											   LinphoneMediaDirectionSendRecv, LinphoneConferenceSecurityLevelNone,
	//											   {LinphoneParticipantRoleSpeaker, LinphoneParticipantRoleListener});
	//

	/*
	auto security_level = LinphoneConferenceSecurityLevelNone;
	bool_t encrypted_conference = (security_level == LinphoneConferenceSecurityLevelEndToEnd ? TRUE : FALSE);
	const LinphoneTesterLimeAlgo lime_algo = encrypted_conference ? C25519 : UNSET;

	Focus focus("chloe_rc");
	std::list<LinphoneTest::ClientConference*> clients;
	LinphoneTest::ClientConference marie("marie_rc", focus.getConferenceFactoryAddress(), lime_algo);
	clients.push_back(&marie);
	LinphoneTest::ClientConference pauline("pauline_rc", focus.getConferenceFactoryAddress(), lime_algo);
	clients.push_back(&pauline);
	LinphoneTest::ClientConference laure("laure_tcp_rc", focus.getConferenceFactoryAddress(), lime_algo);
	clients.push_back(&laure);
	LinphoneTest::ClientConference michelle("michelle_rc", focus.getConferenceFactoryAddress(), lime_algo);
	clients.push_back(&michelle);
	LinphoneTest::ClientConference berthe("berthe_rc", focus.getConferenceFactoryAddress(), lime_algo);
	clients.push_back(&berthe);


*/
	/*
	WrapperTools::createConferenceWithScreenSharing(focus, clients, ms_time(NULL), -1, LinphoneMediaEncryptionNone,
	                                           LinphoneConferenceLayoutActiveSpeaker, TRUE, TRUE, FALSE,
	                                           LinphoneMediaDirectionSendRecv, LinphoneConferenceSecurityLevelNone,
	                                           {LinphoneParticipantRoleSpeaker, LinphoneParticipantRoleListener});
*/
	WrapperTools::activateVideo(screenSharer);
	WrapperTools::activateVideo(marie);
	WrapperTools::activateVideo(pauline);
	// Create conference
	auto confAddr = WrapperTools::createConferenceWithScreenSharing(
	    focus, clientConferences, ms_time(NULL), -1, LinphoneMediaEncryptionNone, LinphoneConferenceLayoutActiveSpeaker,
	    TRUE, LinphoneMediaDirectionSendRecv, LinphoneConferenceSecurityLevelNone,
	    {LinphoneParticipantRoleSpeaker, LinphoneParticipantRoleListener});
	// Create persistant windows
	MSOglContextInfo screenSharerContext, screenSharerPreviewContext;
	void *sharerWindowId = NULL, *sharerPreviewWindowId = NULL;
	WrapperTools::setWindowId(screenSharer, screenSharerContext, &sharerWindowId, "Berthe", false);
	WrapperTools::setWindowId(screenSharer, screenSharerPreviewContext, &sharerPreviewWindowId, "Berthe Preview", true);

	MSOglContextInfo paulineContext, paulinePreviewContext;
	void *paulineWindowId = NULL, *paulinePreviewWindowId = NULL;
	WrapperTools::setWindowId(pauline, paulineContext, &paulineWindowId, "Pauline", false);
	WrapperTools::setWindowId(pauline, paulinePreviewContext, &paulinePreviewWindowId, "Pauline Preview", true);

	MSOglContextInfo marieContext, mariePreviewContext;
	void *marieWindowId = NULL, *mariePreviewWindowId = NULL;
	WrapperTools::setWindowId(marie, marieContext, &marieWindowId, "Marie", false);
	WrapperTools::setWindowId(marie, mariePreviewContext, &mariePreviewWindowId, "Marie Preview", true);

	/*
	params = pauline->createCallParams(pauline->getCurrentCall());
	filepath = bc_tester_file("record-pauline.mkv");
	remove(filepath);
	params->setRecordFile(filepath);
	pauline->getCurrentCall()->setParams(params);
	pauline->getCurrentCall()->update(params);
	bctbx_free(filepath);
	*/
	// wait_for_list(coresList, NULL, 1, 1000);
	// auto nativeId = screenSharer->getCurrentCall()->getNativeVideoWindowId();

	// nativeId = marie->getCurrentCall()->getNativeVideoWindowId();
	// setWindowTitle(nativeId, "Marie");
	wait_for_list(coresList, NULL, 1, 100);
	// Activate ScreenSharing
	WrapperTools::activateScreenSharing(focus, clientConferences, screenSharerCore, confAddr, TRUE, TRUE,
	                                    LinphoneMediaDirectionSendRecv,
	                                    {LinphoneParticipantRoleSpeaker, LinphoneParticipantRoleListener});
	/*
	    auto params = marie->createCallParams(marie->getCurrentCall());
	    params->setRecordFile("/home/julienw/projects/sdk-55/build/debug/linphone-sdk/record-marie.mkv");
	    marie->getCurrentCall()->update(params);
	    params = pauline->createCallParams(pauline->getCurrentCall());
	    params->setRecordFile("/home/julienw/projects/sdk-55/build/debug/linphone-sdk/record-pauline.mkv");
	    pauline->getCurrentCall()->update(params);
	    wait_for_list(coresList, NULL, 1, 100);
	    +*/

	// marie->getCurrentCall()->startRecording();
	// pauline->getCurrentCall()->startRecording();
	// wait_for_list(coresList, NULL, 1, 5000);
	// marie->getCurrentCall()->stopRecording();
	// pauline->getCurrentCall()->stopRecording();

	// screenSharer->setNativeVideoWindowId(sharerWindowId);
	// screenSharer->setNativePreviewWindowId(sharerPreviewWindowId);
	wait_for_list(coresList, NULL, 1, 100);

	lInfo() << "Test: Reloading Devices";
	lInfo() << "CameraEnabled: " << screenSharer->getCurrentCall()->cameraEnabled() << " / "
	        << screenSharer->getCurrentCall()->getRemoteParams()->cameraEnabled() << " / "
	        << screenSharer->getCurrentCall()->getCurrentParams()->cameraEnabled() << " / "
	        << screenSharer->getCurrentCall()->getParams()->cameraEnabled();
	wait_for_list(coresList, NULL, 1, 5000);
	screenSharer->reloadVideoDevices();
	wait_for_list(coresList, NULL, 1, 5000);
	lInfo() << "CameraEnabled: " << screenSharer->getCurrentCall()->cameraEnabled() << " / "
	        << screenSharer->getCurrentCall()->getRemoteParams()->cameraEnabled() << " / "
	        << screenSharer->getCurrentCall()->getCurrentParams()->cameraEnabled() << " / "
	        << screenSharer->getCurrentCall()->getParams()->cameraEnabled();

	WrapperTools::deleteClients(clientConferences);
	linphone_address_unref(confAddr);

	// Linphone::Tester::CoreManager marieManager("marie_rc");
	// Linphone::Tester::CoreManager paulineManager(transport_supported(LinphoneTransportTcp) ? "pauline_rc" :
	// "pauline_tcp_rc"); LinphoneCoreManager *marieC = linphone_core_manager_new("marie_rc"); LinphoneCoreManager
	// *paulineC =
	//   linphone_core_manager_new(transport_supported(LinphoneTransportTcp) ? "pauline_rc" : "pauline_tcp_rc");

	/*
	auto marie = linphone::Object::cPtrToSharedPtr<linphone::Core>(&marieManager.getCCore(), TRUE);
	auto pauline = linphone::Object::cPtrToSharedPtr<linphone::Core>(&paulineManager.getCCore(), TRUE);


	std::shared_ptr<AutoAcceptHandler> handler = std::make_shared<AutoAcceptHandler>();
	pauline->addListener(handler);
	marie->addListener(handler);

	marie->setVideoDisplayFilter("MSOGL");
	pauline->setVideoDisplayFilter("MSOGL");

	marie->enableVideoPreview(true);
	pauline->enableVideoPreview(true);
	marie->enableVideoCapture(true);
	pauline->enableVideoCapture(true);
	marie->enableVideoDisplay(true);
	pauline->enableVideoDisplay(true);
*/
	/*
	    auto marieAccount = marie->getDefaultAccount();
	    auto marieAccountParams = marieAccount->getParams()->clone();
	    marieAccountParams->setConferenceFactoryAddress(factoryAddr);

	    marieAccountParams->setAudioVideoConferenceFactoryAddress(factoryAddr);
	    marieAccount->setParams(marieAccountParams);
	*/
	/*
	auto confParams = marie->createConferenceParams(nullptr);
	BC_ASSERT_PTR_NOT_NULL(confParams);
	confParams->enableVideo(true);
	confParams->setSubject("Screensharing test");
	//confParams->enableLocalParticipant(true);
	confParams->enableChat(false);
	confParams->setAccount(marie->getDefaultAccount());

	auto conference = marie->createConferenceWithParams(confParams);
	BC_ASSERT_PTR_NOT_NULL(conference);

	std::list<std::shared_ptr<linphone::Address>> participants;

	participants.push_back(pauline->getDefaultAccount()->getContactAddress());

	auto callParams = marie->createCallParams(nullptr);
	callParams->enableVideo(true);
	//callParams->setAudioDirection(linphone::MediaDirection::SendRecv);


	bctbx_list_t *participants_info = NULL;
	std::map<LinphoneCoreManager *, LinphoneParticipantInfo *> participantList;

	participantList.insert(std::make_pair(
	    paulineManager.getCMgr(), add_participant_info_to_list(&participants_info,
	paulineManager.getCMgr()->identity,LinphoneParticipantRoleSpeaker, -1))); participantList.insert(std::make_pair(
	        marieManager.getCMgr(), add_participant_info_to_list(&participants_info, marieManager.getCMgr()->identity,
	                                                      LinphoneParticipantRoleListener,
	                                                      -1)));

	LinphoneAddress *confAddr = create_conference_on_server(focus, marieManager, participantList, -1, -1, "subject",
	                                                        "desc", FALSE, LinphoneConferenceSecurityLevelNone, TRUE,
	FALSE, NULL); BC_ASSERT_PTR_NULL(confAddr);

	//conference->inviteParticipants(participants, callParams);

	Linphone::Tester::CoreManagerAssert({marieManager, paulineManager}).waitUntil(std::chrono::seconds(5),[](){return
	false;});

	auto marieCall = marie->getCurrentCall();
	//auto marieCall = marie->getCallByRemoteAddress2(pauline->getDefaultAccount()->getContactAddress());
	auto paulineCall = pauline->getCurrentCall();

*/
	/*
	    auto callParams = marie->createCallParams(nullptr);
	    callParams->enableVideo(true);
	    callParams->enableAudio(true);


	    auto marieStats = marieManager.getStats();
	    //auto marieCall = marie->inviteAddress(pauline->getDefaultAccount()->getContactAddress());
	    auto marieCall = marie->inviteAddressWithParams(pauline->getDefaultAccount()->getContactAddress(), callParams);


	    BC_ASSERT_TRUE(Linphone::Tester::CoreManagerAssert({marieManager, paulineManager}).wait([&marieCall] {
	                        return marieCall->getState() == linphone::Call::State::StreamsRunning;
	    }));

	    //BC_ASSERT_TRUE(wait_for_until(marieC->lc, paulineC->lc, &marieC->stat.number_of_LinphoneCallStreamsRunning,
	   marieStats.number_of_LinphoneCallStreamsRunning+1, 10000)); marieStats = marieManager.getStats();
	    //auto paulineStats = paulineManager.getStats();
	    auto paulineCall = pauline->getCurrentCall();
	    auto paulineCallParams = pauline->createCallParams(paulineCall);
	    paulineCallParams->enableVideo(true);
	    paulineCall->update(paulineCallParams);
	*/

	/*
	auto marieCallParams = marie->createCallParams(marieCall);
	marieCallParams->enableVideo(true);
	marieCallParams->enableScreenSharing(true);
	marieCall->update(marieCallParams);

	BC_ASSERT_TRUE(Linphone::Tester::CoreManagerAssert({marieManager, paulineManager}).wait([&marieCall, &paulineCall] {
	                    return marieCall->getCurrentParams()->videoEnabled() &&
	paulineCall->getCurrentParams()->videoEnabled();
	}));

	///BC_ASSERT_TRUE(wait_for_until(marieC->lc, paulineC->lc, &marieC->stat.number_of_LinphoneCallStreamsRunning,
	marieStats.number_of_LinphoneCallStreamsRunning+1, 10000));
	//BC_ASSERT_TRUE(wait_for_until(marieC->lc, paulineC->lc, &paulineC->stat.number_of_LinphoneCallStreamsRunning,
	paulineStats.number_of_LinphoneCallStreamsRunning+1, 10000));

	auto marieNativeId = marieCall->getNativeVideoWindowId();
	setWindowTitle(marieNativeId, "MARIE");
	auto paulineNativeId = paulineCall->getNativeVideoWindowId();
	setWindowTitle(paulineNativeId, "PAULINE");

	auto videoSourceDescriptor = linphone::Factory::get()->createVideoSourceDescriptor();
	videoSourceDescriptor->setScreenSharing(linphone::VideoSourceScreenSharingType::Display, (void*) 0);
	marieCall->setVideoSource(videoSourceDescriptor);

	BC_ASSERT_TRUE(Linphone::Tester::CoreManagerAssert({marieManager, paulineManager}).wait([&marieCall] {
	                    return marieCall->getCurrentParams()->screenSharingEnabled();
	}));

	Linphone::Tester::CoreManagerAssert({marieManager, paulineManager}).waitUntil(std::chrono::seconds(5),[](){return
	false;});

	//marieCall->getCurrentParams()->copy()->enableScreenSharing(true);

	//marieStats = marieManager.getStats();





	//BC_ASSERT_TRUE(wait_for_until(marieC->lc, paulineC->lc, &marieC->stat.number_of_LinphoneCallStreamsRunning,
	marieStats.number_of_LinphoneCallStreamsRunning+1, 10000));

	//BC_ASSERT_TRUE(wait_for_until(marieC->lc, paulineC->lc, NULL, 0, 5000));

	marie->reloadVideoDevices();

	Linphone::Tester::CoreManagerAssert({marieManager, paulineManager}).waitUntil(std::chrono::seconds(5),[](){return
	false;});
	//BC_ASSERT_TRUE(wait_for_until(marieC->lc, paulineC->lc, NULL, 0, 5000));
*/
	/*
	    int screenIndex = 0;
	    video_call_base_2(marie, pauline, FALSE, LinphoneMediaEncryptionNone, TRUE, TRUE);
	    auto marieCall = linphone_core_get_current_call(marie->lc);
	    if (marieCall) {
	        LinphoneVideoSourceDescriptor * videoSource = linphone_video_source_descriptor_new();

	        linphone_video_source_descriptor_set_screen_sharing(videoSource, LinphoneVideoSourceScreenSharingDisplay,
	   getDisplayIndex(screenIndex)); linphone_call_set_video_source(marieCall, videoSource);

	        linphone_video_source_descriptor_unref(videoSource);
	    }

	    end_call(marie, pauline);
	*/
	/*
	marie = nullptr;
	pauline = nullptr;
	*/
	// linphone_core_manager_destroy(marieC);
	// linphone_core_manager_destroy(paulineC);
}

static void subscribe_replaced(void) {
	LinphoneCoreManager *marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager *pauline = linphone_core_manager_new("pauline_tcp_rc");

	// Get C++ and start working from it.
	auto marieCore = linphone::Object::cPtrToSharedPtr<linphone::Core>(marie->lc, TRUE);
	auto paulineCore = linphone::Object::cPtrToSharedPtr<linphone::Core>(pauline->lc, TRUE);

	bctbx_list_t *lcs = bctbx_list_append(NULL, marie->lc);
	lcs = bctbx_list_append(lcs, pauline->lc);

	auto content = marieCore->createContent();
	content->setType("application");
	content->setSubtype("somexml");
	content->setUtf8Text("<somexml>cxxcxxcxx</somexml>");

	int subscribe_expire = 30;
	auto paulineIdentity = paulineCore->getDefaultAccount()->getParams()->getIdentityAddress()->clone();
	auto ev = marieCore->createSubscribe(paulineIdentity, "cxxdodo", subscribe_expire);
	ev->sendSubscribe(content);

	BC_ASSERT_TRUE(wait_for_list(lcs, &marie->stat.number_of_LinphoneSubscriptionOutgoingProgress, 1,
	                             liblinphone_tester_sip_timeout));
	BC_ASSERT_TRUE(wait_for_list(lcs, &pauline->stat.number_of_LinphoneSubscriptionIncomingReceived, 1,
	                             liblinphone_tester_sip_timeout));

	BC_ASSERT_TRUE(
	    wait_for_list(lcs, &marie->stat.number_of_LinphoneSubscriptionActive, 1, liblinphone_tester_sip_timeout));
	BC_ASSERT_TRUE(
	    wait_for_list(lcs, &pauline->stat.number_of_LinphoneSubscriptionActive, 1, liblinphone_tester_sip_timeout));

	ev->terminate();
	BC_ASSERT_TRUE(
	    wait_for_list(lcs, &pauline->stat.number_of_LinphoneSubscriptionTerminated, 1, liblinphone_tester_sip_timeout));
	BC_ASSERT_TRUE(
	    wait_for_list(lcs, &marie->stat.number_of_LinphoneSubscriptionTerminated, 1, liblinphone_tester_sip_timeout));

	auto anotherContent = marieCore->createContent();
	anotherContent->setType("application");
	anotherContent->setSubtype("somexml");
	anotherContent->setUtf8Text("<somexml>anothercxxanothercxxanothercxx</somexml>");

	ev = marieCore->subscribe(paulineIdentity, "anothercxxdodo", subscribe_expire, anotherContent);
	BC_ASSERT_TRUE(
	    wait_for_list(lcs, &marie->stat.number_of_LinphoneSubscriptionTerminated, 1, liblinphone_tester_sip_timeout));

	BC_ASSERT_TRUE(wait_for_list(lcs, &marie->stat.number_of_LinphoneSubscriptionOutgoingProgress, 2,
	                             liblinphone_tester_sip_timeout));
	BC_ASSERT_TRUE(wait_for_list(lcs, &pauline->stat.number_of_LinphoneSubscriptionIncomingReceived, 2,
	                             liblinphone_tester_sip_timeout));

	BC_ASSERT_TRUE(
	    wait_for_list(lcs, &marie->stat.number_of_LinphoneSubscriptionActive, 2, liblinphone_tester_sip_timeout));
	BC_ASSERT_TRUE(
	    wait_for_list(lcs, &pauline->stat.number_of_LinphoneSubscriptionActive, 2, liblinphone_tester_sip_timeout));

	ev->terminate();
	ev = nullptr;
	BC_ASSERT_TRUE(
	    wait_for_list(lcs, &marie->stat.number_of_LinphoneSubscriptionTerminated, 2, liblinphone_tester_sip_timeout));
	BC_ASSERT_TRUE(
	    wait_for_list(lcs, &pauline->stat.number_of_LinphoneSubscriptionTerminated, 2, liblinphone_tester_sip_timeout));

	bctbx_list_free(lcs);
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

test_t wrapper_cpp_tests[] = {TEST_NO_TAG("Create account", create_account),
                              TEST_NO_TAG("Account freed after core destroyed", account_freed_after_core_destroyed),
                              TEST_NO_TAG("Create chat room", create_chat_room),
                              TEST_NO_TAG("Create conference", create_conference),
                              TEST_NO_TAG("Various API checks", various_api_checks),
                              TEST_NO_TAG("Subscribe replaced", subscribe_replaced),
                              TEST_NO_TAG("Displaying PayloadType", displaying_payload_type),
                              TEST_NO_TAG("Screen sharing", screen_sharing)};

test_suite_t wrapper_cpp_test_suite = {"Wrapper Cpp",
                                       NULL,
                                       NULL,
                                       liblinphone_tester_before_each,
                                       liblinphone_tester_after_each,
                                       sizeof(wrapper_cpp_tests) / sizeof(wrapper_cpp_tests[0]),
                                       wrapper_cpp_tests,
                                       0};
