#include "stdafx.h"
#include "PlayerConnection.h"
#include "ServerPlayer.h"
#include "ServerLevel.h"
#include "ServerPlayerGameMode.h"
#include "PlayerList.h"
#include "MinecraftServer.h"
#include "..\Minecraft.World\net.minecraft.commands.h"
#include "..\Minecraft.World\net.minecraft.network.h"
#include "..\Minecraft.World\net.minecraft.world.entity.item.h"
#include "..\Minecraft.World\net.minecraft.world.level.h"
#include "..\Minecraft.World\net.minecraft.world.level.dimension.h"
#include "..\Minecraft.World\net.minecraft.world.item.h"
#include "..\Minecraft.World\net.minecraft.world.item.trading.h"
#include "..\Minecraft.World\net.minecraft.world.inventory.h"
#include "..\Minecraft.World\net.minecraft.world.level.tile.entity.h"
#include "..\Minecraft.World\net.minecraft.world.level.saveddata.h"
#include "..\Minecraft.World\net.minecraft.network.h"
#include "..\Minecraft.World\net.minecraft.world.food.h"
#include "..\Minecraft.World\AABB.h"
#include "..\Minecraft.World\Pos.h"
#include "..\Minecraft.World\SharedConstants.h"
#include "..\Minecraft.World\Socket.h"
#include "..\Minecraft.World\Achievements.h"
#include "..\Minecraft.World\net.minecraft.h"
#include "EntityTracker.h"
#include "ServerConnection.h"
#include "..\Minecraft.World\GenericStats.h"
#include "..\Minecraft.World\JavaMath.h"
#include <cwctype>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>
#include "..\Minecraft.World\InputOutputStream.h"
#ifdef _WINDOWS64
#include "Windows64\Network\WinsockNetLayer.h"
#endif
// 4J Added
#include "..\Minecraft.World\net.minecraft.world.item.crafting.h"
#include "Options.h"

Random PlayerConnection::random;

namespace
{
	const wstring LCE_TRANSFER_PACKET = L"LCE|Xfer";
	const unsigned int MINIGAME_SETTINGS_FLAG = 0x80000000u;
	const unsigned int MINIGAME_SETTINGS_TYPE_MASK = 0x70000000u;
	const unsigned int MINIGAME_SETTINGS_TYPE_SHIFT = 28u;
	const unsigned int MINIGAME_SETTINGS_ROLE_MASK = 0x0C000000u;
	const unsigned int MINIGAME_SETTINGS_ROLE_SHIFT = 26u;
	const unsigned int MINIGAME_SETTINGS_QUEUE_MASK = 0x03000000u;
	const unsigned int MINIGAME_SETTINGS_QUEUE_SHIFT = 24u;
	const unsigned int MINIGAME_TYPE_BEDWARS = 3u;
	const unsigned int MINIGAME_ROLE_HUB = 0u;
	const unsigned int MINIGAME_ROLE_MATCH = 1u;
	const int PARTY_INVITE_TICKS = SharedConstants::TICKS_PER_SECOND * 30;

	struct BedwarsQueueDef
	{
		const wchar_t *name;
		int npcOffX;
		int npcOffZ;
		int queueOffX;
		int queueOffZ;
		float queueYaw;
		int partyLimit;
		int requiredPlayers;
	};

	static const BedwarsQueueDef BEDWARS_QUEUE_DEFS[] =
	{
		{ L"Solo",    -3,  0, -8,  0,  90.0f, 1, 2 },
		{ L"Doubles",  0,  3,  0,  8, 180.0f, 2, 4 },
		{ L"Squads",   3,  0,  8,  0, 270.0f, 4, 8 },
		{ L"Practice", 0, -3,  0, -8,   0.0f, 1, 1 },
	};

	struct PartyInvite
	{
		BYTE leaderSmallId;
		int expireTick;
	};

	struct BedwarsQueueEntry
	{
		BYTE leaderSmallId;
		vector<BYTE> members;
		int queuedTick;
	};

	static map<BYTE, BYTE> s_partyLeaderByMember;
	static map<BYTE, set<BYTE> > s_partyMembersByLeader;
	static map<BYTE, PartyInvite> s_partyInvites;
	static vector<BedwarsQueueEntry> s_bedwarsQueueEntries[4];
	static map<BYTE, int> s_queueIndexByMember;
	static int s_lastQueueProcessTick = -1;

	struct ProxyRoute
	{
		string hostIp;
		int hostPort;
		wstring displayName;
	};

	static map<wstring, ProxyRoute> s_proxyRoutes;
	static bool s_proxyRoutesLoaded = false;

	wstring ToLowerText(const wstring &value)
	{
		wstring result = value;
		for (size_t i = 0; i < result.length(); ++i)
		{
			result[i] = (wchar_t)std::towlower(result[i]);
		}
		return result;
	}

	vector<wstring> SplitCommandTokens(const wstring &message)
	{
		vector<wstring> tokens;
		size_t pos = 0;
		while (pos < message.length())
		{
			while (pos < message.length() && std::iswspace(message[pos]))
			{
				++pos;
			}
			if (pos >= message.length())
			{
				break;
			}
			size_t end = pos;
			while (end < message.length() && !std::iswspace(message[end]))
			{
				++end;
			}
			tokens.push_back(message.substr(pos, end - pos));
			pos = end;
		}
		return tokens;
	}

	string TrimAsciiString(const string &value)
	{
		size_t first = 0;
		while (first < value.length() && std::isspace((unsigned char)value[first]))
		{
			++first;
		}

		size_t last = value.length();
		while (last > first && std::isspace((unsigned char)value[last - 1]))
		{
			--last;
		}

		return value.substr(first, last - first);
	}

	wstring AsciiToWide(const string &value)
	{
		wstring out;
		out.reserve(value.length());
		for (size_t i = 0; i < value.length(); ++i)
		{
			out.push_back((wchar_t)(unsigned char)value[i]);
		}
		return out;
	}

	bool OpenProxyRoutesFile(std::ifstream &in, string &outPath)
	{
		vector<string> candidates;
		candidates.push_back("proxy-worlds.properties");
		candidates.push_back("..\\proxy-worlds.properties");
		candidates.push_back("..\\..\\proxy-worlds.properties");
		candidates.push_back("..\\..\\..\\proxy-worlds.properties");

		char modulePath[MAX_PATH] = {0};
		if (::GetModuleFileNameA(NULL, modulePath, MAX_PATH) > 0)
		{
			string exePath(modulePath);
			size_t slash = exePath.find_last_of("\\/");
			if (slash != string::npos)
			{
				string exeDir = exePath.substr(0, slash);
				candidates.push_back(exeDir + "\\proxy-worlds.properties");
				candidates.push_back(exeDir + "\\..\\proxy-worlds.properties");
				candidates.push_back(exeDir + "\\..\\..\\proxy-worlds.properties");
			}
		}

		for (size_t i = 0; i < candidates.size(); ++i)
		{
			in.clear();
			in.open(candidates[i].c_str(), std::ios::in);
			if (in.good())
			{
				outPath = candidates[i];
				return true;
			}
		}

		return false;
	}

	void LoadProxyRoutes(bool forceReload)
	{
		if (s_proxyRoutesLoaded && !forceReload)
		{
			return;
		}

		s_proxyRoutesLoaded = true;
		s_proxyRoutes.clear();

		std::ifstream in;
		string loadedFromPath;
		if (!OpenProxyRoutesFile(in, loadedFromPath))
		{
			return;
		}

		string line;
		while (std::getline(in, line))
		{
			const size_t commentPos = line.find('#');
			if (commentPos != string::npos)
			{
				line = line.substr(0, commentPos);
			}

			line = TrimAsciiString(line);
			if (line.empty())
			{
				continue;
			}

			const size_t equalsPos = line.find('=');
			if (equalsPos == string::npos)
			{
				continue;
			}

			string routeNameRaw = TrimAsciiString(line.substr(0, equalsPos));
			string routeSpecRaw = TrimAsciiString(line.substr(equalsPos + 1));
			if (routeNameRaw.empty() || routeSpecRaw.empty())
			{
				continue;
			}

			string displayNameRaw = routeNameRaw;
			const size_t pipePos = routeSpecRaw.find('|');
			if (pipePos != string::npos)
			{
				displayNameRaw = TrimAsciiString(routeSpecRaw.substr(pipePos + 1));
				routeSpecRaw = TrimAsciiString(routeSpecRaw.substr(0, pipePos));
			}

			const size_t colonPos = routeSpecRaw.rfind(':');
			if (colonPos == string::npos)
			{
				continue;
			}

			string ip = TrimAsciiString(routeSpecRaw.substr(0, colonPos));
			string portText = TrimAsciiString(routeSpecRaw.substr(colonPos + 1));
			if (ip.empty() || portText.empty())
			{
				continue;
			}

			char *portEnd = NULL;
			long parsedPort = std::strtol(portText.c_str(), &portEnd, 10);
			if (portEnd == NULL || *portEnd != '\0' || parsedPort <= 0 || parsedPort > 65535)
			{
				continue;
			}

			ProxyRoute route;
			route.hostIp = ip;
			route.hostPort = (int)parsedPort;
			route.displayName = AsciiToWide(displayNameRaw);
			if (route.displayName.empty())
			{
				route.displayName = AsciiToWide(routeNameRaw);
			}

			s_proxyRoutes[ToLowerText(AsciiToWide(routeNameRaw))] = route;
		}
	}

	bool TryGetProxyRoute(const wstring &routeName, ProxyRoute &outRoute)
	{
		LoadProxyRoutes(false);
		auto it = s_proxyRoutes.find(ToLowerText(routeName));
		if (it == s_proxyRoutes.end())
		{
			return false;
		}
		outRoute = it->second;
		return true;
	}

	int GetQueueIndexFromName(const wstring &name)
	{
		const wstring lower = ToLowerText(name);
		if (lower == L"solo") return 0;
		if (lower == L"double" || lower == L"doubles") return 1;
		if (lower == L"squad" || lower == L"squads") return 2;
		if (lower == L"practice") return 3;
		return -1;
	}

	wstring GetQueueDisplayName(int queueIndex)
	{
		if (queueIndex < 0 || queueIndex >= (int)(sizeof(BEDWARS_QUEUE_DEFS) / sizeof(BEDWARS_QUEUE_DEFS[0])))
		{
			return L"Unknown";
		}
		return BEDWARS_QUEUE_DEFS[queueIndex].name;
	}

	BYTE GetSmallIdForPlayer(shared_ptr<ServerPlayer> serverPlayer)
	{
		if (serverPlayer == NULL || serverPlayer->connection == NULL)
		{
			return 0xFF;
		}
		INetworkPlayer *networkPlayer = serverPlayer->connection->getNetworkPlayer();
		if (networkPlayer == NULL)
		{
			return 0xFF;
		}
		return networkPlayer->GetSmallId();
	}

	shared_ptr<ServerPlayer> FindPlayerBySmallId(MinecraftServer *server, BYTE smallId)
	{
		if (server == NULL || server->getPlayers() == NULL)
		{
			return nullptr;
		}
		for (AUTO_VAR(it, server->getPlayers()->players.begin()); it != server->getPlayers()->players.end(); ++it)
		{
			shared_ptr<ServerPlayer> check = *it;
			if (GetSmallIdForPlayer(check) == smallId)
			{
				return check;
			}
		}
		return nullptr;
	}

	shared_ptr<ServerPlayer> FindPlayerByNameInsensitive(MinecraftServer *server, const wstring &name)
	{
		if (server == NULL || server->getPlayers() == NULL)
		{
			return nullptr;
		}
		const wstring wanted = ToLowerText(name);
		for (AUTO_VAR(it, server->getPlayers()->players.begin()); it != server->getPlayers()->players.end(); ++it)
		{
			shared_ptr<ServerPlayer> check = *it;
			if (ToLowerText(check->name) == wanted)
			{
				return check;
			}
		}
		return nullptr;
	}

	wstring GetPlayerNameBySmallId(MinecraftServer *server, BYTE smallId)
	{
		shared_ptr<ServerPlayer> target = FindPlayerBySmallId(server, smallId);
		if (target != NULL)
		{
			return target->name;
		}
		wchar_t fallback[32];
		swprintf_s(fallback, L"Player%u", (unsigned int)smallId);
		return fallback;
	}

	void SendPlayerMessage(shared_ptr<ServerPlayer> target, const wstring &message)
	{
		if (target != NULL)
		{
			target->sendMessage(message, ChatPacket::e_ChatCustom);
		}
	}

	bool IsBedwarsHostSettings(unsigned int hostSettings)
	{
		if ((hostSettings & MINIGAME_SETTINGS_FLAG) == 0)
		{
			return false;
		}
		return (((hostSettings & MINIGAME_SETTINGS_TYPE_MASK) >> MINIGAME_SETTINGS_TYPE_SHIFT) == MINIGAME_TYPE_BEDWARS);
	}

	unsigned int GetBedwarsRoleFromSettings(unsigned int hostSettings)
	{
		return (hostSettings & MINIGAME_SETTINGS_ROLE_MASK) >> MINIGAME_SETTINGS_ROLE_SHIFT;
	}

	unsigned int GetBedwarsQueueModeFromSettings(unsigned int hostSettings)
	{
		return (hostSettings & MINIGAME_SETTINGS_QUEUE_MASK) >> MINIGAME_SETTINGS_QUEUE_SHIFT;
	}

	bool IsBedwarsHubSettings(unsigned int hostSettings)
	{
		if (!IsBedwarsHostSettings(hostSettings))
		{
			return false;
		}
		return GetBedwarsRoleFromSettings(hostSettings) == MINIGAME_ROLE_HUB;
	}

	unsigned int ApplyBedwarsSessionMetadata(unsigned int hostSettings, unsigned int role, unsigned int queueMode)
	{
		hostSettings |= MINIGAME_SETTINGS_FLAG;
		hostSettings &= ~MINIGAME_SETTINGS_TYPE_MASK;
		hostSettings |= (MINIGAME_TYPE_BEDWARS << MINIGAME_SETTINGS_TYPE_SHIFT);
		hostSettings &= ~MINIGAME_SETTINGS_ROLE_MASK;
		hostSettings |= ((role & 0x3u) << MINIGAME_SETTINGS_ROLE_SHIFT);
		hostSettings &= ~MINIGAME_SETTINGS_QUEUE_MASK;
		hostSettings |= ((queueMode & 0x3u) << MINIGAME_SETTINGS_QUEUE_SHIFT);
		return hostSettings;
	}

	BYTE GetPartyLeaderForMember(BYTE memberSmallId)
	{
		auto it = s_partyLeaderByMember.find(memberSmallId);
		if (it == s_partyLeaderByMember.end())
		{
			return memberSmallId;
		}
		return it->second;
	}

	void EnsurePartyLeaderEntry(BYTE leaderSmallId)
	{
		if (s_partyMembersByLeader.find(leaderSmallId) == s_partyMembersByLeader.end())
		{
			s_partyMembersByLeader[leaderSmallId] = set<BYTE>();
		}
		s_partyMembersByLeader[leaderSmallId].insert(leaderSmallId);
		s_partyLeaderByMember[leaderSmallId] = leaderSmallId;
	}

	vector<BYTE> GetPartyMembersForPlayer(BYTE memberSmallId)
	{
		vector<BYTE> members;
		const BYTE leader = GetPartyLeaderForMember(memberSmallId);
		auto it = s_partyMembersByLeader.find(leader);
		if (it == s_partyMembersByLeader.end())
		{
			members.push_back(memberSmallId);
			return members;
		}
		for (AUTO_VAR(itM, it->second.begin()); itM != it->second.end(); ++itM)
		{
			members.push_back(*itM);
		}
		if (members.empty())
		{
			members.push_back(memberSmallId);
		}
		return members;
	}

	void RemoveQueueGroupContainingMember(MinecraftServer *server, BYTE memberSmallId, bool announce, const wchar_t *reason)
	{
		auto queueIt = s_queueIndexByMember.find(memberSmallId);
		if (queueIt == s_queueIndexByMember.end())
		{
			return;
		}
		const int queueIndex = queueIt->second;
		if (queueIndex < 0 || queueIndex >= (int)(sizeof(BEDWARS_QUEUE_DEFS) / sizeof(BEDWARS_QUEUE_DEFS[0])))
		{
			s_queueIndexByMember.erase(memberSmallId);
			return;
		}

		vector<BedwarsQueueEntry> &entries = s_bedwarsQueueEntries[queueIndex];
		for (size_t i = 0; i < entries.size(); ++i)
		{
			bool contains = false;
			for (size_t m = 0; m < entries[i].members.size(); ++m)
			{
				if (entries[i].members[m] == memberSmallId)
				{
					contains = true;
					break;
				}
			}
			if (!contains)
			{
				continue;
			}

			for (size_t m = 0; m < entries[i].members.size(); ++m)
			{
				const BYTE removeId = entries[i].members[m];
				s_queueIndexByMember.erase(removeId);
				if (announce)
				{
					shared_ptr<ServerPlayer> target = FindPlayerBySmallId(server, removeId);
					if (target != NULL)
					{
						wstring msg = L"Left Bedwars ";
						msg += GetQueueDisplayName(queueIndex);
						msg += L" queue";
						if (reason != NULL && reason[0] != 0)
						{
							msg += L" (";
							msg += reason;
							msg += L")";
						}
						msg += L".";
						SendPlayerMessage(target, msg);
					}
				}
			}

			entries.erase(entries.begin() + i);
			return;
		}

		// Defensive cleanup: stale map entry with no corresponding queue group.
		s_queueIndexByMember.erase(memberSmallId);
	}

	void RemovePlayerFromAllQueues(MinecraftServer *server, BYTE memberSmallId, bool announce, const wchar_t *reason)
	{
		if (memberSmallId == 0xFF)
		{
			return;
		}
		while (s_queueIndexByMember.find(memberSmallId) != s_queueIndexByMember.end())
		{
			RemoveQueueGroupContainingMember(server, memberSmallId, announce, reason);
		}
	}

	void LeaveParty(MinecraftServer *server, BYTE memberSmallId, bool announce)
	{
		if (memberSmallId == 0xFF)
		{
			return;
		}

		const BYTE leaderSmallId = GetPartyLeaderForMember(memberSmallId);
		auto partyIt = s_partyMembersByLeader.find(leaderSmallId);
		if (partyIt == s_partyMembersByLeader.end())
		{
			s_partyLeaderByMember.erase(memberSmallId);
			s_partyInvites.erase(memberSmallId);
			return;
		}

		if (leaderSmallId == memberSmallId)
		{
			for (AUTO_VAR(it, partyIt->second.begin()); it != partyIt->second.end(); ++it)
			{
				const BYTE other = *it;
				s_partyLeaderByMember.erase(other);
				if (announce)
				{
					shared_ptr<ServerPlayer> target = FindPlayerBySmallId(server, other);
					if (target != NULL)
					{
						SendPlayerMessage(target, L"Party disbanded.");
					}
				}
			}
			s_partyMembersByLeader.erase(leaderSmallId);
		}
		else
		{
			partyIt->second.erase(memberSmallId);
			s_partyLeaderByMember.erase(memberSmallId);
			if (announce)
			{
				shared_ptr<ServerPlayer> leader = FindPlayerBySmallId(server, leaderSmallId);
				if (leader != NULL)
				{
					wstring leftMsg = GetPlayerNameBySmallId(server, memberSmallId);
					leftMsg += L" left your party.";
					SendPlayerMessage(leader, leftMsg);
				}
				shared_ptr<ServerPlayer> member = FindPlayerBySmallId(server, memberSmallId);
				if (member != NULL)
				{
					SendPlayerMessage(member, L"You left the party.");
				}
			}
			if (partyIt->second.size() <= 1)
			{
				s_partyMembersByLeader.erase(leaderSmallId);
				s_partyLeaderByMember.erase(leaderSmallId);
			}
		}

		s_partyInvites.erase(memberSmallId);
		for (AUTO_VAR(itI, s_partyInvites.begin()); itI != s_partyInvites.end();)
		{
			if (itI->second.leaderSmallId == memberSmallId)
			{
				itI = s_partyInvites.erase(itI);
			}
			else
			{
				++itI;
			}
		}
	}

	void ClearPlayerStateOnDisconnect(MinecraftServer *server, shared_ptr<ServerPlayer> leavingPlayer)
	{
		const BYTE smallId = GetSmallIdForPlayer(leavingPlayer);
		if (smallId == 0xFF)
		{
			return;
		}
		RemovePlayerFromAllQueues(server, smallId, false, NULL);
		LeaveParty(server, smallId, false);
		s_partyInvites.erase(smallId);
	}

	vector<BYTE> BuildQueueGroupMembers(MinecraftServer *server, BYTE requestorSmallId)
	{
		vector<BYTE> members;
		vector<BYTE> partyMembers = GetPartyMembersForPlayer(requestorSmallId);
		for (size_t i = 0; i < partyMembers.size(); ++i)
		{
			if (FindPlayerBySmallId(server, partyMembers[i]) != NULL)
			{
				members.push_back(partyMembers[i]);
			}
		}
		if (members.empty())
		{
			members.push_back(requestorSmallId);
		}
		std::sort(members.begin(), members.end());
		members.erase(std::unique(members.begin(), members.end()), members.end());
		return members;
	}

	bool JoinBedwarsQueue(MinecraftServer *server, shared_ptr<ServerPlayer> requestingPlayer, int queueIndex)
	{
		if (server == NULL || requestingPlayer == NULL)
		{
			return false;
		}
		if (queueIndex < 0 || queueIndex >= (int)(sizeof(BEDWARS_QUEUE_DEFS) / sizeof(BEDWARS_QUEUE_DEFS[0])))
		{
			return false;
		}

		const BYTE requestorSmallId = GetSmallIdForPlayer(requestingPlayer);
		if (requestorSmallId == 0xFF)
		{
			return false;
		}

		vector<BYTE> members = BuildQueueGroupMembers(server, requestorSmallId);
		const BedwarsQueueDef &queue = BEDWARS_QUEUE_DEFS[queueIndex];
		if ((int)members.size() > queue.partyLimit)
		{
			wstring msg = L"Your party is too large for ";
			msg += queue.name;
			msg += L" queue.";
			SendPlayerMessage(requestingPlayer, msg);
			return false;
		}

		for (size_t i = 0; i < members.size(); ++i)
		{
			RemovePlayerFromAllQueues(server, members[i], false, NULL);
		}

		BedwarsQueueEntry entry;
		entry.leaderSmallId = GetPartyLeaderForMember(requestorSmallId);
		entry.members = members;
		entry.queuedTick = server->tickCount;
		s_bedwarsQueueEntries[queueIndex].push_back(entry);
		for (size_t i = 0; i < members.size(); ++i)
		{
			s_queueIndexByMember[members[i]] = queueIndex;
		}

		for (size_t i = 0; i < members.size(); ++i)
		{
			shared_ptr<ServerPlayer> target = FindPlayerBySmallId(server, members[i]);
			if (target != NULL)
			{
				wstring queuedMsg = L"Queued for Bedwars ";
				queuedMsg += queue.name;
				queuedMsg += L".";
				SendPlayerMessage(target, queuedMsg);
			}
		}
		return true;
	}

	bool IsBedwarsHubProtectedArea(ServerLevel *level, int x, int y, int z)
	{
		if (level == NULL || !IsBedwarsHubSettings(app.GetGameHostOption(eGameHostOption_All)))
		{
			return false;
		}
		Pos *spawnPos = level->getSharedSpawnPos();
		if (spawnPos == NULL)
		{
			return false;
		}
		const int dx = (int)Mth::abs((float)(x - spawnPos->x));
		const int dz = (int)Mth::abs((float)(z - spawnPos->z));
		const int minY = spawnPos->y - 3;
		const int maxY = spawnPos->y + 12;
		delete spawnPos;
		return (dx <= 24 && dz <= 24 && y >= minY && y <= maxY);
	}

	int FindBedwarsQueueIndexForTarget(ServerLevel *level, shared_ptr<Entity> target)
	{
		if (level == NULL || target == NULL || target->GetType() != eTYPE_VILLAGER)
		{
			return -1;
		}

		Pos *spawnPos = level->getSharedSpawnPos();
		if (spawnPos == NULL)
		{
			return -1;
		}

		const double spawnX = (double)spawnPos->x + 0.5;
		const double spawnZ = (double)spawnPos->z + 0.5;
		delete spawnPos;

		const double maxMatchDistSqr = 2.5 * 2.5;
		for (size_t i = 0; i < (sizeof(BEDWARS_QUEUE_DEFS) / sizeof(BEDWARS_QUEUE_DEFS[0])); ++i)
		{
			const double npcX = spawnX + (double)BEDWARS_QUEUE_DEFS[i].npcOffX;
			const double npcZ = spawnZ + (double)BEDWARS_QUEUE_DEFS[i].npcOffZ;
			const double dx = target->x - npcX;
			const double dz = target->z - npcZ;
			if ((dx * dx + dz * dz) <= maxMatchDistSqr)
			{
				return (int)i;
			}
		}

		return -1;
	}

#ifdef _WINDOWS64
	bool PickQueueEntriesRecursive(const vector<BedwarsQueueEntry> &entries, size_t startIndex, int remainingPlayers, vector<size_t> &pickedEntries)
	{
		if (remainingPlayers == 0)
		{
			return true;
		}
		if (startIndex >= entries.size() || remainingPlayers < 0)
		{
			return false;
		}

		for (size_t i = startIndex; i < entries.size(); ++i)
		{
			const int groupSize = (int)entries[i].members.size();
			if (groupSize <= 0 || groupSize > remainingPlayers)
			{
				continue;
			}

			pickedEntries.push_back(i);
			if (PickQueueEntriesRecursive(entries, i + 1, remainingPlayers - groupSize, pickedEntries))
			{
				return true;
			}
			pickedEntries.pop_back();
		}

		return false;
	}

	bool PickQueueEntriesForMatch(const vector<BedwarsQueueEntry> &entries, int requiredPlayers, vector<size_t> &pickedEntries)
	{
		pickedEntries.clear();
		if (requiredPlayers <= 0)
		{
			return false;
		}
		return PickQueueEntriesRecursive(entries, 0, requiredPlayers, pickedEntries);
	}

	bool IsRemoteMatchServerCandidate(const Win64LANSession &session, int queueIndex, int requiredPlayers)
	{
		if (!session.isJoinable)
		{
			return false;
		}
		if (!IsBedwarsHostSettings(session.gameHostSettings))
		{
			return false;
		}
		if (GetBedwarsRoleFromSettings(session.gameHostSettings) != MINIGAME_ROLE_MATCH)
		{
			return false;
		}
		if ((int)GetBedwarsQueueModeFromSettings(session.gameHostSettings) != queueIndex)
		{
			return false;
		}
		int maxPlayers = (int)session.maxPlayers;
		if (maxPlayers <= 0)
		{
			maxPlayers = MINECRAFT_NET_MAX_PLAYERS;
		}
		const int slotsAvailable = maxPlayers - (int)session.playerCount;
		if (slotsAvailable < requiredPlayers)
		{
			return false;
		}
		return true;
	}

	bool FindBestQueueTarget(int queueIndex, int requiredPlayers, string &outIp, int &outPort)
	{
		std::vector<Win64LANSession> sessions = WinsockNetLayer::GetDiscoveredSessions();
		int bestFreeSlots = -1;
		int bestPlayers = 9999;
		for (size_t i = 0; i < sessions.size(); ++i)
		{
			const Win64LANSession &session = sessions[i];
			if (!IsRemoteMatchServerCandidate(session, queueIndex, requiredPlayers))
			{
				continue;
			}
			int maxPlayers = (int)session.maxPlayers;
			if (maxPlayers <= 0)
			{
				maxPlayers = MINECRAFT_NET_MAX_PLAYERS;
			}
			const int freeSlots = maxPlayers - (int)session.playerCount;
			if (freeSlots > bestFreeSlots || (freeSlots == bestFreeSlots && (int)session.playerCount < bestPlayers))
			{
				bestFreeSlots = freeSlots;
				bestPlayers = (int)session.playerCount;
				outIp = session.hostIP;
				outPort = session.hostPort;
			}
		}
		return (bestFreeSlots >= 0 && !outIp.empty() && outPort > 0);
	}

	void SendTransferPayloadEx(shared_ptr<ServerPlayer> player, const string &hostIp, int hostPort, int queueIndex, const wstring &destinationName)
	{
		if (player == NULL || player->connection == NULL)
		{
			return;
		}

		wstring hostIpW;
		for (size_t i = 0; i < hostIp.length(); ++i)
		{
			hostIpW.push_back((wchar_t)hostIp[i]);
		}

		ByteArrayOutputStream rawOutput;
		DataOutputStream output(&rawOutput);
		output.writeByte((byte)1);
		output.writeByte((byte)queueIndex);
		output.writeInt(hostPort);
		output.writeUTF(hostIpW);
		output.writeUTF(destinationName);

		player->connection->send(shared_ptr<CustomPayloadPacket>(new CustomPayloadPacket(LCE_TRANSFER_PACKET, rawOutput.toByteArray())));
	}

	void SendTransferPayload(shared_ptr<ServerPlayer> player, const string &hostIp, int hostPort, int queueIndex)
	{
		SendTransferPayloadEx(player, hostIp, hostPort, queueIndex, GetQueueDisplayName(queueIndex));
	}
#endif

	void ProcessBedwarsQueueSystem(MinecraftServer *server)
	{
		if (server == NULL)
		{
			return;
		}
		if (server->getPlayers() == NULL || server->getPlayers()->players.empty())
		{
			for (int i = 0; i < (int)(sizeof(BEDWARS_QUEUE_DEFS) / sizeof(BEDWARS_QUEUE_DEFS[0])); ++i)
			{
				s_bedwarsQueueEntries[i].clear();
			}
			s_queueIndexByMember.clear();
			s_partyInvites.clear();
			s_partyLeaderByMember.clear();
			s_partyMembersByLeader.clear();
			return;
		}

		if (s_lastQueueProcessTick == server->tickCount)
		{
			return;
		}
		s_lastQueueProcessTick = server->tickCount;

		for (AUTO_VAR(itInvite, s_partyInvites.begin()); itInvite != s_partyInvites.end();)
		{
			if (server->tickCount >= itInvite->second.expireTick)
			{
				itInvite = s_partyInvites.erase(itInvite);
			}
			else
			{
				++itInvite;
			}
		}

		for (int queueIndex = 0; queueIndex < (int)(sizeof(BEDWARS_QUEUE_DEFS) / sizeof(BEDWARS_QUEUE_DEFS[0])); ++queueIndex)
		{
			vector<BedwarsQueueEntry> &entries = s_bedwarsQueueEntries[queueIndex];
			for (size_t i = 0; i < entries.size();)
			{
				bool valid = true;
				for (size_t m = 0; m < entries[i].members.size(); ++m)
				{
					if (FindPlayerBySmallId(server, entries[i].members[m]) == NULL)
					{
						valid = false;
						break;
					}
				}
				if (!valid)
				{
					for (size_t m = 0; m < entries[i].members.size(); ++m)
					{
						s_queueIndexByMember.erase(entries[i].members[m]);
					}
					entries.erase(entries.begin() + i);
					continue;
				}
				++i;
			}
		}

#ifdef _WINDOWS64
		if (!IsBedwarsHubSettings(app.GetGameHostOption(eGameHostOption_All)))
		{
			return;
		}

		for (int queueIndex = 0; queueIndex < (int)(sizeof(BEDWARS_QUEUE_DEFS) / sizeof(BEDWARS_QUEUE_DEFS[0])); ++queueIndex)
		{
			vector<BedwarsQueueEntry> &entries = s_bedwarsQueueEntries[queueIndex];
			if (entries.empty())
			{
				continue;
			}

			const int requiredPlayers = BEDWARS_QUEUE_DEFS[queueIndex].requiredPlayers;
			int totalQueued = 0;
			for (size_t i = 0; i < entries.size(); ++i)
			{
				totalQueued += (int)entries[i].members.size();
			}
			if (totalQueued < requiredPlayers)
			{
				continue;
			}

			string targetIp;
			int targetPort = 0;
			if (!FindBestQueueTarget(queueIndex, requiredPlayers, targetIp, targetPort))
			{
				continue;
			}

			vector<size_t> pickedEntries;
			if (!PickQueueEntriesForMatch(entries, requiredPlayers, pickedEntries))
			{
				continue;
			}

			app.DebugPrintf("Bedwars queue %d matched %d players -> %s:%d\n", queueIndex, requiredPlayers, targetIp.c_str(), targetPort);

			for (int pick = (int)pickedEntries.size() - 1; pick >= 0; --pick)
			{
				const size_t entryIndex = pickedEntries[pick];
				if (entryIndex >= entries.size())
				{
					continue;
				}
				const BedwarsQueueEntry entry = entries[entryIndex];
				for (size_t m = 0; m < entry.members.size(); ++m)
				{
					const BYTE memberId = entry.members[m];
					s_queueIndexByMember.erase(memberId);
					shared_ptr<ServerPlayer> target = FindPlayerBySmallId(server, memberId);
					if (target != NULL)
					{
						SendPlayerMessage(target, L"Match found. Transferring to game server...");
						SendTransferPayload(target, targetIp, targetPort, queueIndex);
					}
				}
				entries.erase(entries.begin() + entryIndex);
			}
		}
#endif
	}
}
PlayerConnection::PlayerConnection(MinecraftServer *server, Connection *connection, shared_ptr<ServerPlayer> player)
{
	// 4J - added initialisers
	done = false;
	tickCount = 0;
	aboveGroundTickCount = 0;
	xLastOk = yLastOk = zLastOk = 0;
	synched = true;
	didTick = false;
	lastKeepAliveId = 0;
	lastKeepAliveTime = 0;
	lastKeepAliveTick = 0;
	chatSpamTickCount = 0;
	dropSpamTickCount = 0;

	this->server = server;
	this->connection = connection;
	connection->setListener(this);
	this->player = player;
//	player->connection = this;		// 4J - moved out as we can't assign in a ctor
	InitializeCriticalSection(&done_cs);

	m_bCloseOnTick = false;
	m_bWasKicked = false;

	m_friendsOnlyUGC = false;
	m_offlineXUID = INVALID_XUID;
	m_onlineXUID = INVALID_XUID;
	m_bHasClientTickedOnce = false;

	setShowOnMaps(app.GetGameHostOption(eGameHostOption_Gamertags)!=0?true:false);
}

PlayerConnection::~PlayerConnection()
{
	delete connection;
	DeleteCriticalSection(&done_cs);
}

void PlayerConnection::tick()
{
	if( done ) return;

	if( m_bCloseOnTick )
	{
		disconnect( DisconnectPacket::eDisconnect_Closed );
		return;
	}

	didTick = false;
	tickCount++;
	connection->tick();
	if(done) return;

	if ((tickCount - lastKeepAliveTick) > 20 * 1)
	{
		lastKeepAliveTick = tickCount;
		lastKeepAliveTime = System::nanoTime() / 1000000;
		lastKeepAliveId = random.nextInt();
		send( shared_ptr<KeepAlivePacket>( new KeepAlivePacket(lastKeepAliveId) ) );
	}
//        if (!didTick) {
//            player->doTick(false);
//        }
	
	if (chatSpamTickCount > 0)
	{
		chatSpamTickCount--;
	}
	if (dropSpamTickCount > 0)
	{
		dropSpamTickCount--;
	}

	ProcessBedwarsQueueSystem(server);
}

void PlayerConnection::disconnect(DisconnectPacket::eDisconnectReason reason)
{
	EnterCriticalSection(&done_cs);
	if( done )
	{
		LeaveCriticalSection(&done_cs);
		return;
	}

	app.DebugPrintf("PlayerConnection disconect reason: %d\n", reason );
	ClearPlayerStateOnDisconnect(server, player);
	player->disconnect();

	// 4J Stu - Need to remove the player from the receiving list before their socket is NULLed so that we can find another player on their system
	server->getPlayers()->removePlayerFromReceiving( player );
	send( shared_ptr<DisconnectPacket>( new DisconnectPacket(reason) ));
	connection->sendAndQuit();
	// 4J-PB - removed, since it needs to be localised in the language the client is in
	//server->players->broadcastAll( shared_ptr<ChatPacket>( new ChatPacket(L"§e" + player->name + L" left the game.") ) );
	if(getWasKicked())
	{
		server->getPlayers()->broadcastAll( shared_ptr<ChatPacket>( new ChatPacket(player->name, ChatPacket::e_ChatPlayerKickedFromGame) ) );
	}
	else
	{
		server->getPlayers()->broadcastAll( shared_ptr<ChatPacket>( new ChatPacket(player->name, ChatPacket::e_ChatPlayerLeftGame) ) );
	}
	
	server->getPlayers()->remove(player);
	done = true;
	LeaveCriticalSection(&done_cs);
}

void PlayerConnection::handlePlayerInput(shared_ptr<PlayerInputPacket> packet)
{
   player->setPlayerInput(packet->getXa(), packet->getYa(), packet->isJumping(), packet->isSneaking(), packet->getXRot(), packet->getYRot());
}

void PlayerConnection::handleMovePlayer(shared_ptr<MovePlayerPacket> packet)
{
	ServerLevel *level = server->getLevel(player->dimension);

	didTick = true;
	if(synched) m_bHasClientTickedOnce = true;

	if (player->wonGame) return;

	if (!synched)
	{
		double yDiff = packet->y - yLastOk;
		if (packet->x == xLastOk && yDiff * yDiff < 0.01 && packet->z == zLastOk)
		{
			synched = true;
		}
	}

	if (synched)
	{
		if (player->riding != NULL)
		{

			float yRotT = player->yRot;
			float xRotT = player->xRot;
			player->riding->positionRider();
			double xt = player->x;
			double yt = player->y;
			double zt = player->z;
			double xxa = 0;
			double zza = 0;
			if (packet->hasRot)
			{
				yRotT = packet->yRot;
				xRotT = packet->xRot;
			}
			if (packet->hasPos && packet->y == -999 && packet->yView == -999)
			{
				// CraftBukkit start
				if (abs(packet->x) > 1 || abs(packet->z) > 1)
				{
					//System.err.println(player.name + " was caught trying to crash the server with an invalid position.");
#ifndef _CONTENT_PACKAGE
					wprintf(L"%ls was caught trying to crash the server with an invalid position.", player->name.c_str());
#endif
					disconnect(DisconnectPacket::eDisconnect_IllegalPosition);//"Nope!");
					return;
				}
				// CraftBukkit end
				xxa = packet->x;
				zza = packet->z;
			}


			player->onGround = packet->onGround;

			player->doTick(false);
			player->move(xxa, 0, zza);
			player->absMoveTo(xt, yt, zt, yRotT, xRotT);
			player->xd = xxa;
			player->zd = zza;
			if (player->riding != NULL) level->forceTick(player->riding, true);
			if (player->riding != NULL) player->riding->positionRider();
			server->getPlayers()->move(player);
			xLastOk = player->x;
			yLastOk = player->y;
			zLastOk = player->z;
			((Level *)level)->tick(player);

			return;
		}

		if (player->isSleeping())
		{
			player->doTick(false);
			player->absMoveTo(xLastOk, yLastOk, zLastOk, player->yRot, player->xRot);
			((Level *)level)->tick(player);
			return;
		}

		double startY = player->y;
		xLastOk = player->x;
		yLastOk = player->y;
		zLastOk = player->z;


		double xt = player->x;
		double yt = player->y;
		double zt = player->z;

		float yRotT = player->yRot;
		float xRotT = player->xRot;

		if (packet->hasPos && packet->y == -999 && packet->yView == -999)
		{
			packet->hasPos = false;
		}

		if (packet->hasPos)
		{
			xt = packet->x;
			yt = packet->y;
			zt = packet->z;
			double yd = packet->yView - packet->y;
			if (!player->isSleeping() && (yd > 1.65 || yd < 0.1))
			{
				disconnect(DisconnectPacket::eDisconnect_IllegalStance);
//                logger.warning(player->name + " had an illegal stance: " + yd);
				return;
			}
			if (abs(packet->x) > 32000000 || abs(packet->z) > 32000000)
			{
				disconnect(DisconnectPacket::eDisconnect_IllegalPosition);
				return;
			}
		}
		if (packet->hasRot)
		{
			yRotT = packet->yRot;
			xRotT = packet->xRot;
		}

		// 4J Stu Added to stop server player y pos being different than client when flying
		if(player->abilities.mayfly || player->isAllowedToFly() )
		{
			player->abilities.flying = packet->isFlying;
		}
		else player->abilities.flying = false;

		player->doTick(false);
		player->ySlideOffset = 0;
		player->absMoveTo(xLastOk, yLastOk, zLastOk, yRotT, xRotT);

		if (!synched) return;

		double xDist = xt - player->x;
		double yDist = yt - player->y;
		double zDist = zt - player->z;

		double dist = xDist * xDist + yDist * yDist + zDist * zDist;

		// 4J-PB - removing this one for now
		/*if (dist > 100.0f)
		{
//            logger.warning(player->name + " moved too quickly!");
			disconnect(DisconnectPacket::eDisconnect_MovedTooQuickly);
//                System.out.println("Moved too quickly at " + xt + ", " + yt + ", " + zt);
//                teleport(player->x, player->y, player->z, player->yRot, player->xRot);
			return;
		}
		*/

		float r = 1 / 16.0f;
		bool oldOk = level->getCubes(player, player->bb->copy()->shrink(r, r, r))->empty();

		if (player->onGround && !packet->onGround && yDist > 0)
		{
			// assume the player made a jump
			player->causeFoodExhaustion(FoodConstants::EXHAUSTION_JUMP);
		}

		player->move(xDist, yDist, zDist);

		// 4J Stu - It is possible that we are no longer synched (eg By moving into an End Portal), so we should stop any further movement based on this packet
		// Fix for #87764 - Code: Gameplay: Host cannot move and experiences End World Chunks flickering, while in Splitscreen Mode
		// and Fix for #87788 - Code: Gameplay: Client cannot move and experiences End World Chunks flickering, while in Splitscreen Mode
		if (!synched) return;

		player->onGround = packet->onGround;
		// Since server players don't call travel we check food exhaustion
		// here
		player->checkMovementStatistiscs(xDist, yDist, zDist);

		double oyDist = yDist;

		xDist = xt - player->x;
		yDist = yt - player->y;

		// 4J-PB - line below will always be true!
		if (yDist > -0.5 || yDist < 0.5)
		{
			yDist = 0;
		}
		zDist = zt - player->z;
		dist = xDist * xDist + yDist * yDist + zDist * zDist;
		bool fail = false;
		if (dist > 0.25 * 0.25 && !player->isSleeping() && !player->gameMode->isCreative() && !player->isAllowedToFly())
		{
			fail = true;
//            logger.warning(player->name + " moved wrongly!");
//            System.out.println("Got position " + xt + ", " + yt + ", " + zt);
//            System.out.println("Expected " + player->x + ", " + player->y + ", " + player->z);
#ifndef _CONTENT_PACKAGE
			wprintf(L"%ls moved wrongly!\n",player->name.c_str());
			app.DebugPrintf("Got position %f, %f, %f\n", xt,yt,zt);
			app.DebugPrintf("Expected %f, %f, %f\n", player->x, player->y, player->z);
#endif
		}
		player->absMoveTo(xt, yt, zt, yRotT, xRotT);

		bool newOk = level->getCubes(player, player->bb->copy()->shrink(r, r, r))->empty();
		if (oldOk && (fail || !newOk) && !player->isSleeping())
		{
			teleport(xLastOk, yLastOk, zLastOk, yRotT, xRotT);
			return;
		}
		AABB *testBox = player->bb->copy()->grow(r, r, r)->expand(0, -0.55, 0);
		// && server.level.getCubes(player, testBox).size() == 0
		if (!server->isFlightAllowed() && !player->gameMode->isCreative() && !level->containsAnyBlocks(testBox) && !player->isAllowedToFly() )
		{
			if (oyDist >= (-0.5f / 16.0f))
			{
				aboveGroundTickCount++;
				if (aboveGroundTickCount > 80)
				{
//                    logger.warning(player->name + " was kicked for floating too long!");
#ifndef _CONTENT_PACKAGE
					wprintf(L"%ls was kicked for floating too long!\n", player->name.c_str());
#endif
					disconnect(DisconnectPacket::eDisconnect_NoFlying);
					return;
				}
			}
		}
		else
		{
			aboveGroundTickCount = 0;
		}

		player->onGround = packet->onGround;
		server->getPlayers()->move(player);
		player->doCheckFallDamage(player->y - startY, packet->onGround);
	}

}

void PlayerConnection::teleport(double x, double y, double z, float yRot, float xRot, bool sendPacket /*= true*/)
{
	synched = false;
	xLastOk = x;
	yLastOk = y;
	zLastOk = z;
	player->absMoveTo(x, y, z, yRot, xRot);
	// 4J - note that 1.62 is added to the height here as the client connection that receives this will presume it represents y + heightOffset at that end
	// This is different to the way that height is sent back to the server, where it represents the bottom of the player bounding volume
	if(sendPacket) player->connection->send( shared_ptr<MovePlayerPacket>( new MovePlayerPacket::PosRot(x, y + 1.62f, y, z, yRot, xRot, false, false) ) );
}

void PlayerConnection::handlePlayerAction(shared_ptr<PlayerActionPacket> packet)
{
	ServerLevel *level = server->getLevel(player->dimension);

	if (packet->action == PlayerActionPacket::DROP_ITEM)
	{
		player->drop();
		return;
	}
	else if (packet->action == PlayerActionPacket::RELEASE_USE_ITEM)
	{
		player->releaseUsingItem();
		return;
	}
	// 4J Stu - We don't have ops, so just use the levels setting
	bool canEditSpawn = level->canEditSpawn; // = level->dimension->id != 0 || server->players->isOp(player->name);
	bool shouldVerifyLocation = false;
	if (packet->action == PlayerActionPacket::START_DESTROY_BLOCK) shouldVerifyLocation = true;
	if (packet->action == PlayerActionPacket::STOP_DESTROY_BLOCK) shouldVerifyLocation = true;

	int x = packet->x;
	int y = packet->y;
	int z = packet->z;
	if (packet->action == PlayerActionPacket::START_DESTROY_BLOCK && IsBedwarsHubProtectedArea(level, x, y, z))
	{
		player->connection->send(shared_ptr<TileUpdatePacket>(new TileUpdatePacket(x, y, z, level)));
		return;
	}
	if (shouldVerifyLocation)
	{
		double xDist = player->x - (x + 0.5);
		// there is a mismatch between the player's camera and the player's
		// position, so add 1.5 blocks
		double yDist = player->y - (y + 0.5) + 1.5;
		double zDist = player->z - (z + 0.5);
		double dist = xDist * xDist + yDist * yDist + zDist * zDist;
		if (dist > 6 * 6)
		{
			return;
		}
		if (y >= server->getMaxBuildHeight())
		{
			return;
		}
	}
	Pos *spawnPos = level->getSharedSpawnPos();
	int xd = (int) Mth::abs((float)(x - spawnPos->x));
	int zd = (int) Mth::abs((float)(z - spawnPos->z));
	delete spawnPos;
	if (xd > zd) zd = xd;
	if (packet->action == PlayerActionPacket::START_DESTROY_BLOCK)
	{
		if (zd > 16 || canEditSpawn) player->gameMode->startDestroyBlock(x, y, z, packet->face);
		else player->connection->send( shared_ptr<TileUpdatePacket>( new TileUpdatePacket(x, y, z, level) ) );

	}
	else if (packet->action == PlayerActionPacket::STOP_DESTROY_BLOCK)
	{
		player->gameMode->stopDestroyBlock(x, y, z);
		server->getPlayers()->prioritiseTileChanges(x, y, z, level->dimension->id);	// 4J added - make sure that the update packets for this get prioritised over other general world updates
		if (level->getTile(x, y, z) != 0) player->connection->send( shared_ptr<TileUpdatePacket>( new TileUpdatePacket(x, y, z, level) ) );
	}
	else if (packet->action == PlayerActionPacket::ABORT_DESTROY_BLOCK)
	{
		player->gameMode->abortDestroyBlock(x, y, z);
		if (level->getTile(x, y, z) != 0) player->connection->send(shared_ptr<TileUpdatePacket>( new TileUpdatePacket(x, y, z, level)));
	}
	else if (packet->action == PlayerActionPacket::GET_UPDATED_BLOCK)
	{
		double xDist = player->x - (x + 0.5);
		double yDist = player->y - (y + 0.5);
		double zDist = player->z - (z + 0.5);
		double dist = xDist * xDist + yDist * yDist + zDist * zDist;
		if (dist < 16 * 16)
		{
			player->connection->send( shared_ptr<TileUpdatePacket>( new TileUpdatePacket(x, y, z, level) ) );
		}
	}

	// 4J Stu - Don't change the levels state
	//level->canEditSpawn = false;

}

void PlayerConnection::handleUseItem(shared_ptr<UseItemPacket> packet)
{
	ServerLevel *level = server->getLevel(player->dimension);
	shared_ptr<ItemInstance> item = player->inventory->getSelected();
	bool informClient = false;
	int x = packet->getX();
	int y = packet->getY();
	int z = packet->getZ();
	int face = packet->getFace();
	if (face != 255 && IsBedwarsHubProtectedArea(level, x, y, z))
	{
		player->connection->send(shared_ptr<TileUpdatePacket>(new TileUpdatePacket(x, y, z, level)));
		return;
	}
	
	// 4J Stu - We don't have ops, so just use the levels setting
	bool canEditSpawn = level->canEditSpawn; // = level->dimension->id != 0 || server->players->isOp(player->name);
	if (packet->getFace() == 255)
	{
		if (item == NULL) return;
		player->gameMode->useItem(player, level, item);
	}
	else if ((packet->getY() < server->getMaxBuildHeight() - 1) || (packet->getFace() != Facing::UP && packet->getY() < server->getMaxBuildHeight()))
	{
		Pos *spawnPos = level->getSharedSpawnPos();
		int xd = (int) Mth::abs((float)(x - spawnPos->x));
		int zd = (int) Mth::abs((float)(z - spawnPos->z));
		delete spawnPos;
		if (xd > zd) zd = xd;
		if (synched && player->distanceToSqr(x + 0.5, y + 0.5, z + 0.5) < 8 * 8)
		{
			if (zd > 16 || canEditSpawn)
			{
				player->gameMode->useItemOn(player, level, item, x, y, z, face, packet->getClickX(), packet->getClickY(), packet->getClickZ());
			}
		}

		informClient = true;
	}
	else
	{
		//player->connection->send(shared_ptr<ChatPacket>(new ChatPacket("\u00A77Height limit for building is " + server->maxBuildHeight)));
		informClient = true;
	}

	if (informClient)
	{

		player->connection->send( shared_ptr<TileUpdatePacket>( new TileUpdatePacket(x, y, z, level) ) );

		if (face == 0) y--;
		if (face == 1) y++;
		if (face == 2) z--;
		if (face == 3) z++;
		if (face == 4) x--;
		if (face == 5) x++;
		
		// 4J - Fixes an issue where pistons briefly disappear when retracting. The pistons themselves shouldn't have their change from being pistonBase_Id to  pistonMovingPiece_Id
		// directly sent to the client, as this will happen on the client as a result of it actioning (via a tile event) the retraction of the piston locally. However, by putting a switch
		// beside a piston and then performing an action on the side of it facing a piston, the following line of code will send a TileUpdatePacket containing the change to pistonMovingPiece_Id
		// to the client, and this packet is received before the piston retract action happens - when the piston retract then occurs, it doesn't work properly because the piston tile
		// isn't what it is expecting.
		if( level->getTile(x,y,z) != Tile::pistonMovingPiece_Id )
		{
			player->connection->send( shared_ptr<TileUpdatePacket>( new TileUpdatePacket(x, y, z, level) ) );
		}

	}

	item = player->inventory->getSelected();
	if (item != NULL && item->count == 0)
	{
		player->inventory->items[player->inventory->selected] = nullptr;
		item = nullptr;
	}

	if (item == NULL || item->getUseDuration() == 0)
	{
		player->ignoreSlotUpdateHack = true;
		player->inventory->items[player->inventory->selected] = ItemInstance::clone(player->inventory->items[player->inventory->selected]);
		Slot *s = player->containerMenu->getSlotFor(player->inventory, player->inventory->selected);
		player->containerMenu->broadcastChanges();
		player->ignoreSlotUpdateHack = false;

		if (!ItemInstance::matches(player->inventory->getSelected(), packet->getItem()))
		{
			send( shared_ptr<ContainerSetSlotPacket>( new ContainerSetSlotPacket(player->containerMenu->containerId, s->index, player->inventory->getSelected()) ) );
		}
	}

	// 4J Stu - Don't change the levels state
	//level->canEditSpawn = false;

}

void PlayerConnection::onDisconnect(DisconnectPacket::eDisconnectReason reason, void *reasonObjects)
{
	EnterCriticalSection(&done_cs);
	if( done ) return;
//    logger.info(player.name + " lost connection: " + reason);
	// 4J-PB - removed, since it needs to be localised in the language the client is in
	//server->players->broadcastAll( shared_ptr<ChatPacket>( new ChatPacket(L"§e" + player->name + L" left the game.") ) );
	if(getWasKicked())
	{
		server->getPlayers()->broadcastAll( shared_ptr<ChatPacket>( new ChatPacket(player->name, ChatPacket::e_ChatPlayerKickedFromGame) ) );
	}
	else
	{
		server->getPlayers()->broadcastAll( shared_ptr<ChatPacket>( new ChatPacket(player->name, ChatPacket::e_ChatPlayerLeftGame) ) );
	}
	server->getPlayers()->remove(player);
	done = true;
	LeaveCriticalSection(&done_cs);
}

void PlayerConnection::onUnhandledPacket(shared_ptr<Packet> packet)
{
//    logger.warning(getClass() + " wasn't prepared to deal with a " + packet.getClass());
	disconnect(DisconnectPacket::eDisconnect_UnexpectedPacket);
}

void PlayerConnection::send(shared_ptr<Packet> packet)
{
	if( connection->getSocket() != NULL )
	{
		if( !server->getPlayers()->canReceiveAllPackets( player ) )
		{
			// Check if we are allowed to send this packet type
			if( !Packet::canSendToAnyClient(packet) )
			{
				//wprintf(L"Not the systems primary player, so not sending them a packet : %ls / %d\n", player->name.c_str(), packet->getId() );
				return;
			}
		}
		connection->send(packet);
	}
}

// 4J Added
void PlayerConnection::queueSend(shared_ptr<Packet> packet)
{
	if( connection->getSocket() != NULL )
	{
		if( !server->getPlayers()->canReceiveAllPackets( player ) )
		{
			// Check if we are allowed to send this packet type
			if( !Packet::canSendToAnyClient(packet) )
			{
				//wprintf(L"Not the systems primary player, so not queueing them a packet : %ls\n", connection->getSocket()->getPlayer()->GetGamertag() );
				return;
			}
		}
		connection->queueSend(packet);
	}
}

void PlayerConnection::handleSetCarriedItem(shared_ptr<SetCarriedItemPacket> packet)
{
	if (packet->slot < 0 || packet->slot >= Inventory::getSelectionSize())
	{
//        logger.warning(player.name + " tried to set an invalid carried item");
		return;
	}
	player->inventory->selected = packet->slot;
}

void PlayerConnection::handleChat(shared_ptr<ChatPacket> packet)
{
	if(packet == NULL || packet->m_stringArgs.size() == 0)
	{
		return;
	}

	wstring message = packet->m_stringArgs[0];
	if(message.length() > SharedConstants::maxChatLength)
	{
		disconnect(DisconnectPacket::eDisconnect_Overflow);
		return;
	}

	size_t first = 0;
	while(first < message.length() && std::iswspace(message[first]))
	{
		++first;
	}

	size_t last = message.length();
	while(last > first && std::iswspace(message[last - 1]))
	{
		--last;
	}

	message = message.substr(first, last - first);
	if(message.length() == 0)
	{
		return;
	}

	for(size_t i = 0; i < message.length(); ++i)
	{
		wchar_t ch = message[i];
		if(ch < 32 && SharedConstants::acceptableLetters.find(ch) == wstring::npos)
		{
			return;
		}
	}

	if(message[0] == L'/')
	{
		handleCommand(message);
		return;
	}

	server->getPlayers()->broadcastAll(shared_ptr<ChatPacket>(new ChatPacket(player->name, ChatPacket::e_ChatCustom, -1, message)));

	chatSpamTickCount += SharedConstants::TICKS_PER_SECOND;
	if(chatSpamTickCount > SharedConstants::TICKS_PER_SECOND * 10)
	{
		disconnect(DisconnectPacket::eDisconnect_Overflow);
	}
}

void PlayerConnection::handleCommand(const wstring& message)
{
	if (message.length() < 2 || message[0] != L'/')
	{
		return;
	}

	vector<wstring> tokens = SplitCommandTokens(message.substr(1));
	if (tokens.empty())
	{
		return;
	}

	const wstring command = ToLowerText(tokens[0]);
	const BYTE selfSmallId = GetSmallIdForPlayer(player);
	INetworkPlayer *requestPlayer = getNetworkPlayer();
	const bool isHost = (requestPlayer != NULL && requestPlayer->IsHost());
	const bool isPersistedOp = (server != NULL && server->getPlayers() != NULL && server->getPlayers()->isOp(player->name));
	const bool hasAdminPermission = isHost || player->isModerator() || isPersistedOp;

	if (command == L"tps")
	{
		float tps = server->getRecentTps();
		wchar_t tpsText[64];
		swprintf_s(tpsText, L"TPS: %.2f", tps);
		player->sendMessage(tpsText, ChatPacket::e_ChatCustom);
		return;
	}

	if (command == L"help")
	{
		player->sendMessage(L"/tps, /list, /party, /queue, /queuehost, /hub, /server", ChatPacket::e_ChatCustom);
		if (hasAdminPermission)
		{
			player->sendMessage(L"Admin: /kick /ban /pardon /op /deop /tp /gamemode /save-on /save-off /save-all /whitelist /send", ChatPacket::e_ChatCustom);
		}
		return;
	}

	if (command == L"list")
	{
		wchar_t listInfo[80];
		const int count = (server != NULL && server->getPlayers() != NULL) ? server->getPlayers()->getPlayerCount() : 0;
		const int maxPlayers = (server != NULL && server->getPlayers() != NULL) ? server->getPlayers()->getMaxPlayers() : MINECRAFT_NET_MAX_PLAYERS;
		swprintf_s(listInfo, L"Players: %d/%d", count, maxPlayers);
		player->sendMessage(listInfo, ChatPacket::e_ChatCustom);
		if (server != NULL && server->getPlayers() != NULL)
		{
			const wstring names = server->getPlayers()->getPlayerNames();
			if (!names.empty())
			{
				player->sendMessage(names, ChatPacket::e_ChatCustom);
			}
		}
		return;
	}

	if (command == L"server")
	{
		if (tokens.size() < 2)
		{
			player->sendMessage(L"Usage: /server <list|name|reload>", ChatPacket::e_ChatCustom);
			return;
		}

		const wstring sub = ToLowerText(tokens[1]);
		if (sub == L"list")
		{
			LoadProxyRoutes(false);
			if (s_proxyRoutes.empty())
			{
				player->sendMessage(L"No proxy routes configured. Create proxy-worlds.properties.", ChatPacket::e_ChatCustom);
				return;
			}

			player->sendMessage(L"Available servers:", ChatPacket::e_ChatCustom);
			for (AUTO_VAR(itRoute, s_proxyRoutes.begin()); itRoute != s_proxyRoutes.end(); ++itRoute)
			{
				const ProxyRoute &route = itRoute->second;
				wstring line = L" - ";
				line += itRoute->first;
				line += L" (";
				line += route.displayName;
				line += L")";
				player->sendMessage(line, ChatPacket::e_ChatCustom);
			}
			return;
		}

		if (sub == L"reload")
		{
			if (!hasAdminPermission)
			{
				player->sendMessage(L"You do not have permission.", ChatPacket::e_ChatCustom);
				return;
			}
			LoadProxyRoutes(true);
			player->sendMessage(L"Proxy route config reloaded.", ChatPacket::e_ChatCustom);
			return;
		}

		ProxyRoute route;
		if (!TryGetProxyRoute(sub, route))
		{
			player->sendMessage(L"Unknown server route. Use /server list.", ChatPacket::e_ChatCustom);
			return;
		}

		SendTransferPayloadEx(player, route.hostIp, route.hostPort, 0xFF, route.displayName);
		wstring msg = L"Transferring to ";
		msg += route.displayName;
		msg += L"...";
		player->sendMessage(msg, ChatPacket::e_ChatCustom);
		return;
	}

	if (command == L"send")
	{
		if (!hasAdminPermission)
		{
			player->sendMessage(L"You do not have permission.", ChatPacket::e_ChatCustom);
			return;
		}
		if (tokens.size() < 3)
		{
			player->sendMessage(L"Usage: /send <player> <server>", ChatPacket::e_ChatCustom);
			return;
		}

		shared_ptr<ServerPlayer> target = FindPlayerByNameInsensitive(server, tokens[1]);
		if (target == NULL || target->connection == NULL)
		{
			player->sendMessage(L"Player not found.", ChatPacket::e_ChatCustom);
			return;
		}

		ProxyRoute route;
		if (!TryGetProxyRoute(tokens[2], route))
		{
			player->sendMessage(L"Unknown server route. Use /server list.", ChatPacket::e_ChatCustom);
			return;
		}

		SendTransferPayloadEx(target, route.hostIp, route.hostPort, 0xFF, route.displayName);
		wstring adminMsg = L"Sent ";
		adminMsg += target->name;
		adminMsg += L" to ";
		adminMsg += route.displayName;
		player->sendMessage(adminMsg, ChatPacket::e_ChatCustom);
		return;
	}

	if (command == L"whitelist")
	{
		if (!hasAdminPermission)
		{
			player->sendMessage(L"You do not have permission.", ChatPacket::e_ChatCustom);
			return;
		}
		if (tokens.size() < 2)
		{
			player->sendMessage(L"Usage: /whitelist <on|off|add|remove|reload>", ChatPacket::e_ChatCustom);
			return;
		}
		const wstring sub = ToLowerText(tokens[1]);
		if (sub == L"on")
		{
			server->getPlayers()->setWhitelistEnabled(true);
			player->sendMessage(L"Whitelist enabled.", ChatPacket::e_ChatCustom);
			return;
		}
		if (sub == L"off")
		{
			server->getPlayers()->setWhitelistEnabled(false);
			player->sendMessage(L"Whitelist disabled.", ChatPacket::e_ChatCustom);
			return;
		}
		if (sub == L"reload")
		{
			server->getPlayers()->reloadWhitelist();
			player->sendMessage(L"Whitelist reloaded.", ChatPacket::e_ChatCustom);
			return;
		}
		if (sub == L"add" && tokens.size() >= 3)
		{
			server->getPlayers()->whiteList(tokens[2]);
			wstring msg = L"Added to whitelist: ";
			msg += tokens[2];
			player->sendMessage(msg, ChatPacket::e_ChatCustom);
			return;
		}
		if ((sub == L"remove" || sub == L"del") && tokens.size() >= 3)
		{
			server->getPlayers()->blackList(tokens[2]);
			wstring msg = L"Removed from whitelist: ";
			msg += tokens[2];
			player->sendMessage(msg, ChatPacket::e_ChatCustom);
			return;
		}

		player->sendMessage(L"Usage: /whitelist <on|off|add|remove|reload>", ChatPacket::e_ChatCustom);
		return;
	}

	if (command == L"op" || command == L"deop")
	{
		if (!hasAdminPermission)
		{
			player->sendMessage(L"You do not have permission.", ChatPacket::e_ChatCustom);
			return;
		}
		if (tokens.size() < 2)
		{
			player->sendMessage(command == L"op" ? L"Usage: /op <player>" : L"Usage: /deop <player>", ChatPacket::e_ChatCustom);
			return;
		}

		const bool isAdd = (command == L"op");
		const wstring &targetName = tokens[1];
		bool changed = isAdd ? server->getPlayers()->addOp(targetName) : server->getPlayers()->removeOp(targetName);

		shared_ptr<ServerPlayer> target = FindPlayerByNameInsensitive(server, targetName);
		if (target != NULL)
		{
			target->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_Op, isAdd ? 1 : 0);
			server->getPlayers()->broadcastAll(shared_ptr<PlayerInfoPacket>(new PlayerInfoPacket(target)));
		}

		if (changed)
		{
			wstring ok = (isAdd ? L"Granted op to " : L"Removed op from ");
			ok += targetName;
			player->sendMessage(ok, ChatPacket::e_ChatCustom);
		}
		else
		{
			wstring msg = (isAdd ? L"Already op: " : L"Not op: ");
			msg += targetName;
			player->sendMessage(msg, ChatPacket::e_ChatCustom);
		}
		return;
	}

	if (command == L"kick" || command == L"ban" || command == L"pardon")
	{
		if (!hasAdminPermission)
		{
			player->sendMessage(L"You do not have permission.", ChatPacket::e_ChatCustom);
			return;
		}

		if ((command == L"kick" || command == L"ban") && tokens.size() < 2)
		{
			player->sendMessage(command == L"kick" ? L"Usage: /kick <player>" : L"Usage: /ban <player>", ChatPacket::e_ChatCustom);
			return;
		}

		if (command == L"pardon")
		{
			if (tokens.size() < 2)
			{
				player->sendMessage(L"Usage: /pardon <player>", ChatPacket::e_ChatCustom);
				return;
			}
			if (server->getPlayers()->unbanName(tokens[1]))
			{
				wstring msg = L"Unbanned ";
				msg += tokens[1];
				player->sendMessage(msg, ChatPacket::e_ChatCustom);
			}
			else
			{
				player->sendMessage(L"Player was not banned.", ChatPacket::e_ChatCustom);
			}
			return;
		}

		shared_ptr<ServerPlayer> target = FindPlayerByNameInsensitive(server, tokens[1]);
		if (target == NULL || target->connection == NULL)
		{
			player->sendMessage(L"Player not found.", ChatPacket::e_ChatCustom);
			return;
		}
		if (target == player)
		{
			player->sendMessage(L"You cannot target yourself.", ChatPacket::e_ChatCustom);
			return;
		}

		if (command == L"ban")
		{
			server->getPlayers()->banName(target->name);
		}

		target->connection->setWasKicked();
		target->connection->disconnect(command == L"ban" ? DisconnectPacket::eDisconnect_Banned : DisconnectPacket::eDisconnect_Kicked);
		return;
	}

	if (command == L"save-on" || command == L"save-off" || command == L"save-all")
	{
		if (!hasAdminPermission)
		{
			player->sendMessage(L"You do not have permission.", ChatPacket::e_ChatCustom);
			return;
		}
		if (command == L"save-all")
		{
			server->getPlayers()->saveAll(NULL, false);
			player->sendMessage(L"Saved all player data.", ChatPacket::e_ChatCustom);
			return;
		}

		const bool disableSaving = (command == L"save-off");
		app.SetGameHostOption(eGameHostOption_DisableSaving, disableSaving ? 1 : 0);
		player->sendMessage(disableSaving ? L"World saving disabled." : L"World saving enabled.", ChatPacket::e_ChatCustom);
		return;
	}

	if (command == L"gamemode" || command == L"gm")
	{
		if (!hasAdminPermission)
		{
			player->sendMessage(L"You do not have permission.", ChatPacket::e_ChatCustom);
			return;
		}
		if (tokens.size() < 2)
		{
			player->sendMessage(L"Usage: /gamemode <survival|creative|0|1> [player]", ChatPacket::e_ChatCustom);
			return;
		}
		const wstring gmArg = ToLowerText(tokens[1]);
		GameType *gameType = NULL;
		if (gmArg == L"0" || gmArg == L"s" || gmArg == L"survival")
		{
			gameType = GameType::SURVIVAL;
		}
		else if (gmArg == L"1" || gmArg == L"c" || gmArg == L"creative")
		{
			gameType = GameType::CREATIVE;
		}
		if (gameType == NULL)
		{
			player->sendMessage(L"Unknown gamemode.", ChatPacket::e_ChatCustom);
			return;
		}

		shared_ptr<ServerPlayer> target = player;
		if (tokens.size() >= 3)
		{
			target = FindPlayerByNameInsensitive(server, tokens[2]);
			if (target == NULL)
			{
				player->sendMessage(L"Player not found.", ChatPacket::e_ChatCustom);
				return;
			}
		}

		target->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CreativeMode, gameType == GameType::CREATIVE ? 1 : 0);
		target->gameMode->setGameModeForPlayer(gameType);
		target->connection->send(shared_ptr<GameEventPacket>(new GameEventPacket(GameEventPacket::CHANGE_GAME_MODE, gameType->getId())));
		server->getPlayers()->broadcastAll(shared_ptr<PlayerInfoPacket>(new PlayerInfoPacket(target)));
		player->sendMessage(L"Gamemode updated.", ChatPacket::e_ChatCustom);
		return;
	}

	if (command == L"tp")
	{
		if (!hasAdminPermission)
		{
			player->sendMessage(L"You do not have permission.", ChatPacket::e_ChatCustom);
			return;
		}
		if (tokens.size() < 2)
		{
			player->sendMessage(L"Usage: /tp <target> [destination]", ChatPacket::e_ChatCustom);
			return;
		}

		shared_ptr<ServerPlayer> toMove = player;
		shared_ptr<ServerPlayer> destination = NULL;
		if (tokens.size() == 2)
		{
			destination = FindPlayerByNameInsensitive(server, tokens[1]);
		}
		else
		{
			toMove = FindPlayerByNameInsensitive(server, tokens[1]);
			destination = FindPlayerByNameInsensitive(server, tokens[2]);
		}
		if (toMove == NULL || destination == NULL || toMove->connection == NULL)
		{
			player->sendMessage(L"Player not found.", ChatPacket::e_ChatCustom);
			return;
		}

		toMove->connection->teleport(destination->x, destination->y, destination->z, destination->yRot, destination->xRot);
		player->sendMessage(L"Teleported.", ChatPacket::e_ChatCustom);
		return;
	}
	if (command == L"hub" && IsBedwarsHubSettings(app.GetGameHostOption(eGameHostOption_All)))
	{
		ServerLevel *level = server->getLevel(player->dimension);
		if (level != NULL)
		{
			Pos *spawnPos = level->getSharedSpawnPos();
			if (spawnPos != NULL)
			{
				teleport((double)spawnPos->x + 0.5, (double)spawnPos->y + 1.0, (double)spawnPos->z + 0.5, 0.0f, 0.0f);
				delete spawnPos;
			}
		}
		player->sendMessage(L"Returned to Bedwars hub.", ChatPacket::e_ChatCustom);
		return;
	}

	if (command == L"queue" && IsBedwarsHostSettings(app.GetGameHostOption(eGameHostOption_All)))
	{
		if (!IsBedwarsHubSettings(app.GetGameHostOption(eGameHostOption_All)))
		{
			player->sendMessage(L"Queues are only available in Bedwars hub mode.", ChatPacket::e_ChatCustom);
			return;
		}
		if (tokens.size() < 2)
		{
			player->sendMessage(L"Usage: /queue <solo|doubles|squads|practice|leave>", ChatPacket::e_ChatCustom);
			return;
		}
		const wstring queueArg = ToLowerText(tokens[1]);
		if (queueArg == L"leave")
		{
			RemovePlayerFromAllQueues(server, selfSmallId, true, L"left");
			return;
		}
		const int queueIndex = GetQueueIndexFromName(queueArg);
		if (queueIndex < 0)
		{
			player->sendMessage(L"Unknown queue. Use solo, doubles, squads or practice.", ChatPacket::e_ChatCustom);
			return;
		}
		JoinBedwarsQueue(server, player, queueIndex);
		return;
	}

	if (command == L"queuehost" && IsBedwarsHostSettings(app.GetGameHostOption(eGameHostOption_All)))
	{
		INetworkPlayer *networkPlayer = getNetworkPlayer();
		if (networkPlayer == NULL || !networkPlayer->IsHost())
		{
			player->sendMessage(L"Only the host can change queue hosting mode.", ChatPacket::e_ChatCustom);
			return;
		}
		if (tokens.size() < 2)
		{
			player->sendMessage(L"Usage: /queuehost <hub|solo|doubles|squads|practice>", ChatPacket::e_ChatCustom);
			return;
		}

		const wstring hostMode = ToLowerText(tokens[1]);
		unsigned int hostSettings = app.GetGameHostOption(eGameHostOption_All);
		if (hostMode == L"hub")
		{
			hostSettings = ApplyBedwarsSessionMetadata(hostSettings, MINIGAME_ROLE_HUB, 0u);
			app.SetGameHostOption(eGameHostOption_All, hostSettings);
			g_NetworkManager.UpdateAndSetGameSessionData();
			player->sendMessage(L"This server is now a Bedwars hub.", ChatPacket::e_ChatCustom);
			return;
		}

		const int queueIndex = GetQueueIndexFromName(hostMode);
		if (queueIndex < 0)
		{
			player->sendMessage(L"Unknown queue mode for /queuehost.", ChatPacket::e_ChatCustom);
			return;
		}

		hostSettings = ApplyBedwarsSessionMetadata(hostSettings, MINIGAME_ROLE_MATCH, (unsigned int)queueIndex);
		app.SetGameHostOption(eGameHostOption_All, hostSettings);
		g_NetworkManager.UpdateAndSetGameSessionData();

		wstring modeMsg = L"This server now accepts ";
		modeMsg += GetQueueDisplayName(queueIndex);
		modeMsg += L" queue transfers.";
		player->sendMessage(modeMsg, ChatPacket::e_ChatCustom);
		return;
	}

	if (command == L"party")
	{
		if (selfSmallId == 0xFF)
		{
			return;
		}

		if (tokens.size() < 2)
		{
			player->sendMessage(L"Usage: /party <invite|accept|leave|list>", ChatPacket::e_ChatCustom);
			return;
		}

		const wstring sub = ToLowerText(tokens[1]);
		if (sub == L"invite")
		{
			if (tokens.size() < 3)
			{
				player->sendMessage(L"Usage: /party invite <player>", ChatPacket::e_ChatCustom);
				return;
			}

			const BYTE currentLeader = GetPartyLeaderForMember(selfSmallId);
			if (currentLeader != selfSmallId)
			{
				player->sendMessage(L"Only the party leader can invite players.", ChatPacket::e_ChatCustom);
				return;
			}

			shared_ptr<ServerPlayer> target = FindPlayerByNameInsensitive(server, tokens[2]);
			if (target == NULL)
			{
				player->sendMessage(L"Player not found.", ChatPacket::e_ChatCustom);
				return;
			}

			const BYTE targetSmallId = GetSmallIdForPlayer(target);
			if (targetSmallId == 0xFF || targetSmallId == selfSmallId)
			{
				player->sendMessage(L"Invalid party target.", ChatPacket::e_ChatCustom);
				return;
			}

			PartyInvite invite;
			invite.leaderSmallId = selfSmallId;
			invite.expireTick = server->tickCount + PARTY_INVITE_TICKS;
			s_partyInvites[targetSmallId] = invite;

			wstring sentMsg = L"Invited ";
			sentMsg += target->name;
			sentMsg += L" to your party.";
			player->sendMessage(sentMsg, ChatPacket::e_ChatCustom);

			wstring recvMsg = player->name;
			recvMsg += L" invited you to a party. Type /party accept.";
			SendPlayerMessage(target, recvMsg);
			return;
		}

		if (sub == L"accept")
		{
			auto inviteIt = s_partyInvites.find(selfSmallId);
			if (inviteIt == s_partyInvites.end())
			{
				player->sendMessage(L"You have no pending party invite.", ChatPacket::e_ChatCustom);
				return;
			}

			const BYTE leaderSmallId = inviteIt->second.leaderSmallId;
			shared_ptr<ServerPlayer> leader = FindPlayerBySmallId(server, leaderSmallId);
			if (leader == NULL)
			{
				s_partyInvites.erase(selfSmallId);
				player->sendMessage(L"That party invite has expired.", ChatPacket::e_ChatCustom);
				return;
			}

			RemovePlayerFromAllQueues(server, selfSmallId, true, L"party updated");
			LeaveParty(server, selfSmallId, false);
			EnsurePartyLeaderEntry(leaderSmallId);
			s_partyMembersByLeader[leaderSmallId].insert(selfSmallId);
			s_partyLeaderByMember[selfSmallId] = leaderSmallId;
			s_partyInvites.erase(selfSmallId);

			wstring joinedMsg = L"You joined ";
			joinedMsg += leader->name;
			joinedMsg += L"'s party.";
			player->sendMessage(joinedMsg, ChatPacket::e_ChatCustom);

			wstring leaderMsg = player->name;
			leaderMsg += L" joined your party.";
			SendPlayerMessage(leader, leaderMsg);
			return;
		}

		if (sub == L"leave")
		{
			RemovePlayerFromAllQueues(server, selfSmallId, true, L"party left");
			LeaveParty(server, selfSmallId, true);
			return;
		}

		if (sub == L"list")
		{
			const BYTE leaderSmallId = GetPartyLeaderForMember(selfSmallId);
			auto partyIt = s_partyMembersByLeader.find(leaderSmallId);
			if (partyIt == s_partyMembersByLeader.end() || partyIt->second.size() <= 1)
			{
				player->sendMessage(L"You are not in a party.", ChatPacket::e_ChatCustom);
				return;
			}

			wstring listMsg = L"Party: ";
			bool first = true;
			for (AUTO_VAR(itM, partyIt->second.begin()); itM != partyIt->second.end(); ++itM)
			{
				if (!first) listMsg += L", ";
				listMsg += GetPlayerNameBySmallId(server, *itM);
				first = false;
			}
			player->sendMessage(listMsg, ChatPacket::e_ChatCustom);
			return;
		}

		player->sendMessage(L"Usage: /party <invite|accept|leave|list>", ChatPacket::e_ChatCustom);
		return;
	}

	player->sendMessage(L"Unknown command. Use /help.", ChatPacket::e_ChatCustom);
}
void PlayerConnection::handleAnimate(shared_ptr<AnimatePacket> packet)
{
	if (packet->action == AnimatePacket::SWING)
	{
		player->swing();
	}
}

void PlayerConnection::handlePlayerCommand(shared_ptr<PlayerCommandPacket> packet)
{
	if (packet->action == PlayerCommandPacket::START_SNEAKING)
	{
		player->setSneaking(true);
	}
	else if (packet->action == PlayerCommandPacket::STOP_SNEAKING)
	{
		player->setSneaking(false);
	}
	else if (packet->action == PlayerCommandPacket::START_SPRINTING)
	{
		player->setSprinting(true);
	}
	else if (packet->action == PlayerCommandPacket::STOP_SPRINTING)
	{
		player->setSprinting(false);
	}
	else if (packet->action == PlayerCommandPacket::STOP_SLEEPING)
	{
		player->stopSleepInBed(false, true, true);
		synched = false;
	}
	else if (packet->action == PlayerCommandPacket::START_IDLEANIM)
	{
		player->setIsIdle(true);
	}
	else if (packet->action == PlayerCommandPacket::STOP_IDLEANIM)
	{
		player->setIsIdle(false);
	}

}

void PlayerConnection::setShowOnMaps(bool bVal)
{
	player->setShowOnMaps(bVal);
}

void PlayerConnection::handleDisconnect(shared_ptr<DisconnectPacket> packet)
{
	// 4J Stu - Need to remove the player from the receiving list before their socket is NULLed so that we can find another player on their system
	ClearPlayerStateOnDisconnect(server, player);
	server->getPlayers()->removePlayerFromReceiving( player );
	connection->close(DisconnectPacket::eDisconnect_Quitting);
}

int PlayerConnection::countDelayedPackets()
{
	return connection->countDelayedPackets();
}

void PlayerConnection::info(const wstring& string)
{
	// 4J-PB - removed, since it needs to be localised in the language the client is in
	//send( shared_ptr<ChatPacket>( new ChatPacket(L"§7" + string) ) );
}

void PlayerConnection::warn(const wstring& string)
{
	// 4J-PB - removed, since it needs to be localised in the language the client is in
	//send( shared_ptr<ChatPacket>( new ChatPacket(L"§9" + string) ) );
}

wstring PlayerConnection::getConsoleName()
{
	return player->name;
}

void PlayerConnection::handleInteract(shared_ptr<InteractPacket> packet)
{
	ServerLevel *level = server->getLevel(player->dimension);
	shared_ptr<Entity> target = level->getEntity(packet->target);

	// Fix for #8218 - Gameplay: Attacking zombies from a different level often results in no hits being registered
	// 4J Stu - If the client says that we hit something, then agree with it. The canSee can fail here as it checks
	// a ray from head->head, but we may actually be looking at a different part of the entity that can be seen
	// even though the ray is blocked.
	if (target != NULL) // && player->canSee(target) && player->distanceToSqr(target) < 6 * 6)
	{
		if (packet->action == InteractPacket::INTERACT &&
			IsBedwarsHubSettings(app.GetGameHostOption(eGameHostOption_All)) &&
			target->GetType() == eTYPE_VILLAGER)
		{
			const int queueIndex = FindBedwarsQueueIndexForTarget(level, target);
			if (queueIndex >= 0)
			{
				Pos *spawnPos = level->getSharedSpawnPos();
				if (spawnPos != NULL)
				{
					const BedwarsQueueDef &queue = BEDWARS_QUEUE_DEFS[queueIndex];
					const double queueX = (double)spawnPos->x + 0.5 + (double)queue.queueOffX;
					const double queueY = (double)spawnPos->y + 1.0;
					const double queueZ = (double)spawnPos->z + 0.5 + (double)queue.queueOffZ;
					delete spawnPos;

					teleport(queueX, queueY, queueZ, queue.queueYaw, 0.0f);
				}

				JoinBedwarsQueue(server, player, queueIndex);
			}
			else
			{
				send(shared_ptr<ChatPacket>(new ChatPacket(L"Bedwars", ChatPacket::e_ChatCustom, -1, L"This Bedwars NPC is unavailable.")));
			}
			return;
		}

		//boole canSee = player->canSee(target);
		//double maxDist = 6 * 6;
		//if (!canSee)
		//{
		//	maxDist = 3 * 3;
		//}

		//if (player->distanceToSqr(target) < maxDist)
		//{
			if (packet->action == InteractPacket::INTERACT)
			{
				player->interact(target);
			}
			else if (packet->action == InteractPacket::ATTACK)
			{
				player->attack(target);
			}
		//}
	}

}

bool PlayerConnection::canHandleAsyncPackets()
{
	return true;
}

void PlayerConnection::handleTexture(shared_ptr<TexturePacket> packet)
{
	// Both PlayerConnection and ClientConnection should handle this mostly the same way

	if(packet->dwBytes==0)
	{
		// Request for texture
#ifndef _CONTENT_PACKAGE
			wprintf(L"Server received request for custom texture %ls\n",packet->textureName.c_str());
#endif
		PBYTE pbData=NULL;
		DWORD dwBytes=0;		
		app.GetMemFileDetails(packet->textureName,&pbData,&dwBytes);

		if(dwBytes!=0)
		{
			send( shared_ptr<TexturePacket>( new TexturePacket(packet->textureName,pbData,dwBytes) ) );
		}
		else
		{
			m_texturesRequested.push_back( packet->textureName );
		}
	}
	else
	{
		// Response with texture data
#ifndef _CONTENT_PACKAGE
			wprintf(L"Server received custom texture %ls\n",packet->textureName.c_str());
#endif
		app.AddMemoryTextureFile(packet->textureName,packet->pbData,packet->dwBytes);
		server->connection->handleTextureReceived(packet->textureName);
	}
}

void PlayerConnection::handleTextureAndGeometry(shared_ptr<TextureAndGeometryPacket> packet)
{
	// Both PlayerConnection and ClientConnection should handle this mostly the same way

	if(packet->dwTextureBytes==0)
	{
		// Request for texture and geometry
#ifndef _CONTENT_PACKAGE
		wprintf(L"Server received request for custom texture %ls\n",packet->textureName.c_str());
#endif
		PBYTE pbData=NULL;
		DWORD dwTextureBytes=0;		
		app.GetMemFileDetails(packet->textureName,&pbData,&dwTextureBytes);
		DLCSkinFile *pDLCSkinFile = app.m_dlcManager.getSkinFile(packet->textureName);

		if(dwTextureBytes!=0)
		{

			if(pDLCSkinFile)
			{
				if(pDLCSkinFile->getAdditionalBoxesCount()!=0)
				{
					send( shared_ptr<TextureAndGeometryPacket>( new TextureAndGeometryPacket(packet->textureName,pbData,dwTextureBytes,pDLCSkinFile) ) );
				}
				else
				{
					send( shared_ptr<TextureAndGeometryPacket>( new TextureAndGeometryPacket(packet->textureName,pbData,dwTextureBytes) ) );
				}
			}
			else
			{
				// we don't have the dlc skin, so retrieve the data from the app store
				vector<SKIN_BOX *> *pvSkinBoxes = app.GetAdditionalSkinBoxes(packet->dwSkinID);
				unsigned int uiAnimOverrideBitmask= app.GetAnimOverrideBitmask(packet->dwSkinID);

				send( shared_ptr<TextureAndGeometryPacket>( new TextureAndGeometryPacket(packet->textureName,pbData,dwTextureBytes,pvSkinBoxes,uiAnimOverrideBitmask) ) );
			}
		}
		else
		{
			m_texturesRequested.push_back( packet->textureName );
		}
	}
	else
	{
		// Response with texture and geometry data
#ifndef _CONTENT_PACKAGE
		wprintf(L"Server received custom texture %ls and geometry\n",packet->textureName.c_str());
#endif
		app.AddMemoryTextureFile(packet->textureName,packet->pbData,packet->dwTextureBytes);

		// add the geometry to the app list
		if(packet->dwBoxC!=0)
		{
#ifndef _CONTENT_PACKAGE
			wprintf(L"Adding skin boxes for skin id %X, box count %d\n",packet->dwSkinID,packet->dwBoxC);
#endif
			app.SetAdditionalSkinBoxes(packet->dwSkinID,packet->BoxDataA,packet->dwBoxC);
		}
		// Add the anim override
		app.SetAnimOverrideBitmask(packet->dwSkinID,packet->uiAnimOverrideBitmask);

		player->setCustomSkin(packet->dwSkinID);

		server->connection->handleTextureAndGeometryReceived(packet->textureName);
	}
}

void PlayerConnection::handleTextureReceived(const wstring &textureName)
{
	// This sends the server received texture out to any other players waiting for the data
	AUTO_VAR(it, find( m_texturesRequested.begin(), m_texturesRequested.end(), textureName ));
	if( it != m_texturesRequested.end() )
	{
		PBYTE pbData=NULL;
		DWORD dwBytes=0;		
		app.GetMemFileDetails(textureName,&pbData,&dwBytes);

		if(dwBytes!=0)
		{
			send( shared_ptr<TexturePacket>( new TexturePacket(textureName,pbData,dwBytes) ) );
			m_texturesRequested.erase(it);
		}
	}
}

void PlayerConnection::handleTextureAndGeometryReceived(const wstring &textureName)
{
	// This sends the server received texture out to any other players waiting for the data
	AUTO_VAR(it, find( m_texturesRequested.begin(), m_texturesRequested.end(), textureName ));
	if( it != m_texturesRequested.end() )
	{
		PBYTE pbData=NULL;
		DWORD dwTextureBytes=0;		
		app.GetMemFileDetails(textureName,&pbData,&dwTextureBytes);
		DLCSkinFile *pDLCSkinFile=app.m_dlcManager.getSkinFile(textureName);

		if(dwTextureBytes!=0)
		{
			if(pDLCSkinFile && (pDLCSkinFile->getAdditionalBoxesCount()!=0))
			{
				send( shared_ptr<TextureAndGeometryPacket>( new TextureAndGeometryPacket(textureName,pbData,dwTextureBytes,pDLCSkinFile) ) );
			}
			else
			{
				// get the data from the app
				DWORD dwSkinID = app.getSkinIdFromPath(textureName);
				vector<SKIN_BOX *> *pvSkinBoxes = app.GetAdditionalSkinBoxes(dwSkinID);
				unsigned int uiAnimOverrideBitmask= app.GetAnimOverrideBitmask(dwSkinID);

				send( shared_ptr<TextureAndGeometryPacket>( new TextureAndGeometryPacket(textureName,pbData,dwTextureBytes, pvSkinBoxes, uiAnimOverrideBitmask) ) );
			}
			m_texturesRequested.erase(it);		
		}
	}
}

void PlayerConnection::handleTextureChange(shared_ptr<TextureChangePacket> packet)
{
	switch(packet->action)
	{
	case TextureChangePacket::e_TextureChange_Skin:
		player->setCustomSkin( app.getSkinIdFromPath( packet->path ) );
#ifndef _CONTENT_PACKAGE
	wprintf(L"Skin for server player %ls has changed to %ls (%d)\n", player->name.c_str(), player->customTextureUrl.c_str(), player->getPlayerDefaultSkin() );
#endif
		break;
	case TextureChangePacket::e_TextureChange_Cape:
		player->setCustomCape( Player::getCapeIdFromPath( packet->path ) );
		//player->customTextureUrl2 = packet->path;
#ifndef _CONTENT_PACKAGE
	wprintf(L"Cape for server player %ls has changed to %ls\n", player->name.c_str(), player->customTextureUrl2.c_str() );
#endif
		break;
	}
	if(!packet->path.empty() && packet->path.substr(0,3).compare(L"def") != 0 && !app.IsFileInMemoryTextures(packet->path))
	{
		if(	server->connection->addPendingTextureRequest(packet->path))
		{
#ifndef _CONTENT_PACKAGE
			wprintf(L"Sending texture packet to get custom skin %ls from player %ls\n",packet->path.c_str(), player->name.c_str());
#endif
			send(shared_ptr<TexturePacket>( new TexturePacket(packet->path,NULL,0) ) );
		}
	}
	else if(!packet->path.empty() && app.IsFileInMemoryTextures(packet->path))
	{			
		// Update the ref count on the memory texture data
		app.AddMemoryTextureFile(packet->path,NULL,0);
	}
	server->getPlayers()->broadcastAll( shared_ptr<TextureChangePacket>( new TextureChangePacket(player,packet->action,packet->path) ), player->dimension );
}

void PlayerConnection::handleTextureAndGeometryChange(shared_ptr<TextureAndGeometryChangePacket> packet)
{

		player->setCustomSkin( app.getSkinIdFromPath( packet->path ) );
#ifndef _CONTENT_PACKAGE
		wprintf(L"PlayerConnection::handleTextureAndGeometryChange - Skin for server player %ls has changed to %ls (%d)\n", player->name.c_str(), player->customTextureUrl.c_str(), player->getPlayerDefaultSkin() );
#endif
		
	
	if(!packet->path.empty() && packet->path.substr(0,3).compare(L"def") != 0 && !app.IsFileInMemoryTextures(packet->path))
	{
		if(	server->connection->addPendingTextureRequest(packet->path))
		{
#ifndef _CONTENT_PACKAGE
			wprintf(L"Sending texture packet to get custom skin %ls from player %ls\n",packet->path.c_str(), player->name.c_str());
#endif
			send(shared_ptr<TextureAndGeometryPacket>( new TextureAndGeometryPacket(packet->path,NULL,0) ) );
		}
	}
	else if(!packet->path.empty() && app.IsFileInMemoryTextures(packet->path))
	{			
		// Update the ref count on the memory texture data
		app.AddMemoryTextureFile(packet->path,NULL,0);

		player->setCustomSkin(packet->dwSkinID);

		// If we already have the texture, then we already have the model parts too
		//app.SetAdditionalSkinBoxes(packet->dwSkinID,)
		//DebugBreak();
	}
	server->getPlayers()->broadcastAll( shared_ptr<TextureAndGeometryChangePacket>( new TextureAndGeometryChangePacket(player,packet->path) ), player->dimension );
}

void PlayerConnection::handleServerSettingsChanged(shared_ptr<ServerSettingsChangedPacket> packet)
{
	if(packet->action==ServerSettingsChangedPacket::HOST_IN_GAME_SETTINGS)
	{
		// Need to check that this player has permission to change each individual setting?

		INetworkPlayer *networkPlayer = getNetworkPlayer();
		if( (networkPlayer != NULL && networkPlayer->IsHost()) || player->isModerator())
		{
			app.SetGameHostOption(eGameHostOption_FireSpreads, app.GetGameHostOption(packet->data,eGameHostOption_FireSpreads));
			app.SetGameHostOption(eGameHostOption_TNT, app.GetGameHostOption(packet->data,eGameHostOption_TNT));

			server->getPlayers()->broadcastAll( shared_ptr<ServerSettingsChangedPacket>( new ServerSettingsChangedPacket( ServerSettingsChangedPacket::HOST_IN_GAME_SETTINGS,app.GetGameHostOption(eGameHostOption_All) ) ) );

			// Update the QoS data
			g_NetworkManager.UpdateAndSetGameSessionData();
		}
	}
}

void PlayerConnection::handleKickPlayer(shared_ptr<KickPlayerPacket> packet)
{
	INetworkPlayer *networkPlayer = getNetworkPlayer();
	if( (networkPlayer != NULL && networkPlayer->IsHost()) || player->isModerator())
	{		
		server->getPlayers()->kickPlayerByShortId(packet->m_networkSmallId);
	}
}

void PlayerConnection::handleGameCommand(shared_ptr<GameCommandPacket> packet)
{
	MinecraftServer::getInstance()->getCommandDispatcher()->performCommand(player, packet->command, packet->data);
}

void PlayerConnection::handleClientCommand(shared_ptr<ClientCommandPacket> packet)
{
	if (packet->action == ClientCommandPacket::PERFORM_RESPAWN)
	{
		if (player->wonGame)
		{
			player = server->getPlayers()->respawn(player, player->m_enteredEndExitPortal?0:player->dimension, true);
		}
		//else if (player.getLevel().getLevelData().isHardcore())
		//{
		//	if (server.isSingleplayer() && player.name.equals(server.getSingleplayerName()))
		//	{
		//		player.connection.disconnect("You have died. Game over, man, it's game over!");
		//		server.selfDestruct();
		//	}
		//	else
		//	{
		//		BanEntry ban = new BanEntry(player.name);
		//		ban.setReason("Death in Hardcore");

		//		server.getPlayers().getBans().add(ban);
		//		player.connection.disconnect("You have died. Game over, man, it's game over!");
		//	}
		//}
		else
		{
			if (player->getHealth() > 0) return;
			player = server->getPlayers()->respawn(player, 0, false);
		}
	}
}

void PlayerConnection::handleRespawn(shared_ptr<RespawnPacket> packet)
{
}

void PlayerConnection::handleContainerClose(shared_ptr<ContainerClosePacket> packet)
{
	player->doCloseContainer();
}

#ifndef _CONTENT_PACKAGE	
void PlayerConnection::handleContainerSetSlot(shared_ptr<ContainerSetSlotPacket> packet)
{
	if (packet->containerId == AbstractContainerMenu::CONTAINER_ID_CARRIED )
	{
        player->inventory->setCarried(packet->item);
    }
	else
	{
        if (packet->containerId == AbstractContainerMenu::CONTAINER_ID_INVENTORY && packet->slot >= 36 && packet->slot < 36 + 9)
		{
            shared_ptr<ItemInstance> lastItem = player->inventoryMenu->getSlot(packet->slot)->getItem();
            if (packet->item != NULL)
			{
                if (lastItem == NULL || lastItem->count < packet->item->count)
				{
                    packet->item->popTime = Inventory::POP_TIME_DURATION;
                }
            }
			player->inventoryMenu->setItem(packet->slot, packet->item);
			player->ignoreSlotUpdateHack = true;
			player->containerMenu->broadcastChanges();
			player->broadcastCarriedItem();
			player->ignoreSlotUpdateHack = false;
        }
		else if (packet->containerId == player->containerMenu->containerId)
		{
            player->containerMenu->setItem(packet->slot, packet->item);
			player->ignoreSlotUpdateHack = true;
			player->containerMenu->broadcastChanges();
			player->broadcastCarriedItem();
			player->ignoreSlotUpdateHack = false;
        }
    }
}
#endif

void PlayerConnection::handleContainerClick(shared_ptr<ContainerClickPacket> packet)
{
	if (player->containerMenu->containerId == packet->containerId && player->containerMenu->isSynched(player))
	{
		shared_ptr<ItemInstance> clicked = player->containerMenu->clicked(packet->slotNum, packet->buttonNum, packet->quickKey?AbstractContainerMenu::CLICK_QUICK_MOVE:AbstractContainerMenu::CLICK_PICKUP, player);

		if (ItemInstance::matches(packet->item, clicked))
		{
			// Yep, you sure did click what you claimed to click!
			player->connection->send( shared_ptr<ContainerAckPacket>( new ContainerAckPacket(packet->containerId, packet->uid, true) ) );
			player->ignoreSlotUpdateHack = true;
			player->containerMenu->broadcastChanges();
			player->broadcastCarriedItem();
			player->ignoreSlotUpdateHack = false;
		}
		else
		{
			// No, you clicked the wrong thing!
			expectedAcks[player->containerMenu->containerId] = packet->uid;
			player->connection->send( shared_ptr<ContainerAckPacket>( new ContainerAckPacket(packet->containerId, packet->uid, false) ) );
			player->containerMenu->setSynched(player, false);

			vector<shared_ptr<ItemInstance> > items;
			for (unsigned int i = 0; i < player->containerMenu->slots->size(); i++)
			{
				items.push_back(player->containerMenu->slots->at(i)->getItem());
			}
			player->refreshContainer(player->containerMenu, &items);

//                player.containerMenu.broadcastChanges();
		}
	}

}

void PlayerConnection::handleContainerButtonClick(shared_ptr<ContainerButtonClickPacket> packet)
{
	if (player->containerMenu->containerId == packet->containerId && player->containerMenu->isSynched(player))
	{
		player->containerMenu->clickMenuButton(player, packet->buttonId);
		player->containerMenu->broadcastChanges();
	}
}

void PlayerConnection::handleSetCreativeModeSlot(shared_ptr<SetCreativeModeSlotPacket> packet)
{
	if (player->gameMode->isCreative())
	{
		bool drop = packet->slotNum < 0;
		shared_ptr<ItemInstance> item = packet->item;

		if(item != NULL && item->id == Item::map_Id)
		{
			int mapScale = 3;
#ifdef _LARGE_WORLDS
			int scale = MapItemSavedData::MAP_SIZE * 2 * (1 << mapScale);
			int centreXC = (int) (Math::round(player->x / scale) * scale);
			int centreZC = (int) (Math::round(player->z / scale) * scale);
#else
			// 4J-PB - for Xbox maps, we'll centre them on the origin of the world, since we can fit the whole world in our map
			int centreXC = 0;
			int centreZC = 0;
#endif
			item->setAuxValue( player->level->getAuxValueForMap(player->getXuid(), player->dimension, centreXC, centreZC, mapScale) );			

			shared_ptr<MapItemSavedData> data = MapItem::getSavedData(item->getAuxValue(), player->level);
			// 4J Stu - We only have one map per player per dimension, so don't reset the one that they have
			// when a new one is created
			wchar_t buf[64];
			swprintf(buf,64,L"map_%d", item->getAuxValue());
			std::wstring id = wstring(buf);
			if( data == NULL )
			{
				data = shared_ptr<MapItemSavedData>( new MapItemSavedData(id) );
		}
			player->level->setSavedData(id, (shared_ptr<SavedData> ) data);

			data->scale = mapScale;
			// 4J-PB - for Xbox maps, we'll centre them on the origin of the world, since we can fit the whole world in our map
			data->x = centreXC;
			data->z = centreZC;
			data->dimension = (byte) player->level->dimension->id;
			data->setDirty();
		}

		bool validSlot = (packet->slotNum >= InventoryMenu::CRAFT_SLOT_START && packet->slotNum < (InventoryMenu::USE_ROW_SLOT_START + Inventory::getSelectionSize()));
		bool validItem = item == NULL || (item->id < Item::items.length && item->id >= 0 && Item::items[item->id] != NULL);
		bool validData = item == NULL || (item->getAuxValue() >= 0 && item->count > 0 && item->count <= 64);

		if (validSlot && validItem && validData)
		{
			if (item == NULL)
			{
				player->inventoryMenu->setItem(packet->slotNum, nullptr);
			}
			else
			{
				player->inventoryMenu->setItem(packet->slotNum, item );
			}
			player->inventoryMenu->setSynched(player, true);
			//                player.slotChanged(player.inventoryMenu, packet.slotNum, player.inventoryMenu.getSlot(packet.slotNum).getItem());
		}
		else if (drop && validItem && validData)
		{
			if (dropSpamTickCount < SharedConstants::TICKS_PER_SECOND * 10)
			{
				dropSpamTickCount += SharedConstants::TICKS_PER_SECOND;
				// drop item
				shared_ptr<ItemEntity> dropped = player->drop(item);
				if (dropped != NULL)
				{
					dropped->setShortLifeTime();
				}
			}
		}

		if( item != NULL && item->id == Item::map_Id )
		{
			// 4J Stu - Maps need to have their aux value update, so the client should always be assumed to be wrong
			// This is how the Java works, as the client also incorrectly predicts the auxvalue of the mapItem
			vector<shared_ptr<ItemInstance> > items;
			for (unsigned int i = 0; i < player->inventoryMenu->slots->size(); i++)
			{
				items.push_back(player->inventoryMenu->slots->at(i)->getItem());
			}
			player->refreshContainer(player->inventoryMenu, &items);
		}
	}
}

void PlayerConnection::handleContainerAck(shared_ptr<ContainerAckPacket> packet)
{
	AUTO_VAR(it, expectedAcks.find(player->containerMenu->containerId));

	if (it != expectedAcks.end() && packet->uid == it->second && player->containerMenu->containerId == packet->containerId && !player->containerMenu->isSynched(player))
	{
		player->containerMenu->setSynched(player, true);
	}
}

void PlayerConnection::handleSignUpdate(shared_ptr<SignUpdatePacket> packet)
{
	app.DebugPrintf("PlayerConnection::handleSignUpdate\n");

	ServerLevel *level = server->getLevel(player->dimension);
	if (level->hasChunkAt(packet->x, packet->y, packet->z))
	{
		shared_ptr<TileEntity> te = level->getTileEntity(packet->x, packet->y, packet->z);

		if (dynamic_pointer_cast<SignTileEntity>(te) != NULL)
		{
			shared_ptr<SignTileEntity> ste = dynamic_pointer_cast<SignTileEntity>(te);
			if (!ste->isEditable())
			{
				server->warn(L"Player " + player->name + L" just tried to change non-editable sign");
				return;
			}
		}

		// 4J-JEV: Changed to allow characters to display as a [].
		if (dynamic_pointer_cast<SignTileEntity>(te) != NULL)
		{
			int x = packet->x;
			int y = packet->y;
			int z = packet->z;
			shared_ptr<SignTileEntity> ste = dynamic_pointer_cast<SignTileEntity>(te);
			for (int i = 0; i < 4; i++)
			{
				wstring lineText = packet->lines[i].substr(0,15);
				ste->SetMessage( i, lineText );
			}
			ste->SetVerified(false);
			ste->setChanged();
			level->sendTileUpdated(x, y, z);
		}
	}

}

void PlayerConnection::handleKeepAlive(shared_ptr<KeepAlivePacket> packet)
{
	if (packet->id == lastKeepAliveId)
	{
		int time = (int) (System::nanoTime() / 1000000 - lastKeepAliveTime);
		player->latency = (player->latency * 3 + time) / 4;
	}
}

void PlayerConnection::handlePlayerInfo(shared_ptr<PlayerInfoPacket> packet)
{	
	// Need to check that this player has permission to change each individual setting?

	INetworkPlayer *networkPlayer = getNetworkPlayer();
	if( (networkPlayer != NULL && networkPlayer->IsHost()) || player->isModerator() )
	{
		shared_ptr<ServerPlayer> serverPlayer;
		// Find the player being edited
		for(AUTO_VAR(it, server->getPlayers()->players.begin()); it != server->getPlayers()->players.end(); ++it)
		{
			shared_ptr<ServerPlayer> checkingPlayer = *it;
			if(checkingPlayer->connection->getNetworkPlayer() != NULL && checkingPlayer->connection->getNetworkPlayer()->GetSmallId() == packet->m_networkSmallId)
			{
				serverPlayer = checkingPlayer;
				break;
			}
		}

		if(serverPlayer != NULL)
		{
			unsigned int origPrivs = serverPlayer->getAllPlayerGamePrivileges();

			bool trustPlayers = app.GetGameHostOption(eGameHostOption_TrustPlayers) != 0;
			bool cheats = app.GetGameHostOption(eGameHostOption_CheatsEnabled) != 0;
			if(serverPlayer == player)
			{
				GameType *gameType = Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CreativeMode) ? GameType::CREATIVE : GameType::SURVIVAL;
				gameType = LevelSettings::validateGameType(gameType->getId());
				if (serverPlayer->gameMode->getGameModeForPlayer() != gameType)
				{
#ifndef _CONTENT_PACKAGE
					wprintf(L"Setting %ls to game mode %d\n", serverPlayer->name.c_str(), gameType);
#endif
					serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CreativeMode,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CreativeMode) );
					serverPlayer->gameMode->setGameModeForPlayer(gameType);
					serverPlayer->connection->send( shared_ptr<GameEventPacket>( new GameEventPacket(GameEventPacket::CHANGE_GAME_MODE, gameType->getId()) ));
				}
				else
				{
#ifndef _CONTENT_PACKAGE
					wprintf(L"%ls already has game mode %d\n", serverPlayer->name.c_str(), gameType);
#endif
				}
				if(cheats)
				{
					// Editing self
					bool canBeInvisible = Player::getPlayerGamePrivilege(origPrivs, Player::ePlayerGamePrivilege_CanToggleInvisible) != 0;
					if(canBeInvisible)serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_Invisible,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_Invisible) );
					if(canBeInvisible)serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_Invulnerable,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_Invulnerable) );

					bool inCreativeMode = Player::getPlayerGamePrivilege(origPrivs,Player::ePlayerGamePrivilege_CreativeMode) != 0;
					if(!inCreativeMode)
					{
						bool canFly = Player::getPlayerGamePrivilege(origPrivs,Player::ePlayerGamePrivilege_CanToggleFly);
						bool canChangeHunger = Player::getPlayerGamePrivilege(origPrivs,Player::ePlayerGamePrivilege_CanToggleClassicHunger);

						if(canFly)serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CanFly,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CanFly) );
						if(canChangeHunger)serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_ClassicHunger,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_ClassicHunger) );
					}
				}
			}
			else
			{
				// Editing someone else				
				if(!trustPlayers && !serverPlayer->connection->getNetworkPlayer()->IsHost())
				{
					serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CannotMine,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CannotMine) );
					serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CannotBuild,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CannotBuild) );
					serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CannotAttackPlayers,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CannotAttackPlayers) );
					serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CannotAttackAnimals,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CannotAttackAnimals) );
					serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CanUseDoorsAndSwitches,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CanUseDoorsAndSwitches) );
					serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CanUseContainers,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CanUseContainers) );
				}

				if(networkPlayer->IsHost())
				{
					if(cheats)
					{
						serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CanToggleInvisible,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CanToggleInvisible) );
						serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CanToggleFly,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CanToggleFly) );
						serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CanToggleClassicHunger,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CanToggleClassicHunger) );
						serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CanTeleport,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_CanTeleport) );
					}
					serverPlayer->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_Op,Player::getPlayerGamePrivilege(packet->m_playerPrivileges,Player::ePlayerGamePrivilege_Op) );
				}
			}

			server->getPlayers()->broadcastAll( shared_ptr<PlayerInfoPacket>( new PlayerInfoPacket( serverPlayer ) ) );
		}
	}
}

bool PlayerConnection::isServerPacketListener()
{
	return true;
}

void PlayerConnection::handlePlayerAbilities(shared_ptr<PlayerAbilitiesPacket> playerAbilitiesPacket)
{
	player->abilities.flying = playerAbilitiesPacket->isFlying() && player->abilities.mayfly;
}

//void handleChatAutoComplete(ChatAutoCompletePacket packet) {
//	StringBuilder result = new StringBuilder();

//	for (String candidate : server.getAutoCompletions(player, packet.getMessage())) {
//		if (result.length() > 0) result.append("\0");

//		result.append(candidate);
//	}

//	player.connection.send(new ChatAutoCompletePacket(result.toString()));
//}

//void handleClientInformation(shared_ptr<ClientInformationPacket> packet)
//{
//	player->updateOptions(packet);
//}

void PlayerConnection::handleCustomPayload(shared_ptr<CustomPayloadPacket> customPayloadPacket)
{
#if 0
	if (CustomPayloadPacket.CUSTOM_BOOK_PACKET.equals(customPayloadPacket.identifier))
	{
		ByteArrayInputStream bais(customPayloadPacket->data);
		DataInputStream input(&bais);
		shared_ptr<ItemInstance> sentItem = Packet::readItem(input);

		if (!WritingBookItem.makeSureTagIsValid(sentItem.getTag()))
		{
			throw new IOException("Invalid book tag!");
		}

		// make sure the sent item is the currently carried item
		ItemInstance carried = player.inventory.getSelected();
		if (sentItem != null && sentItem.id == Item.writingBook.id && sentItem.id == carried.id)
		{
			carried.setTag(sentItem.getTag());
		}
	}
	else if (CustomPayloadPacket.CUSTOM_BOOK_SIGN_PACKET.equals(customPayloadPacket.identifier))
	{
		DataInputStream input = new DataInputStream(new ByteArrayInputStream(customPayloadPacket.data));
		ItemInstance sentItem = Packet.readItem(input);

		if (!WrittenBookItem.makeSureTagIsValid(sentItem.getTag()))
		{
			throw new IOException("Invalid book tag!");
		}

		// make sure the sent item is the currently carried item
		ItemInstance carried = player.inventory.getSelected();
		if (sentItem != null && sentItem.id == Item.writtenBook.id && carried.id == Item.writingBook.id)
		{
			carried.setTag(sentItem.getTag());
			carried.id = Item.writtenBook.id;
		}
	}
	else
#endif
		if (CustomPayloadPacket::TRADER_SELECTION_PACKET.compare(customPayloadPacket->identifier) == 0)
		{
			ByteArrayInputStream bais(customPayloadPacket->data);
			DataInputStream input(&bais);
			int selection = input.readInt();

			AbstractContainerMenu *menu = player->containerMenu;
			if (dynamic_cast<MerchantMenu *>(menu))
			{
				((MerchantMenu *) menu)->setSelectionHint(selection);
			}
		}
		else if (CustomPayloadPacket::SET_ITEM_NAME_PACKET.compare(customPayloadPacket->identifier) == 0)
		{
			RepairMenu *menu = dynamic_cast<RepairMenu *>( player->containerMenu);
			if (menu)
			{
				if (customPayloadPacket->data.data == NULL || customPayloadPacket->data.length < 1)
				{
					menu->setItemName(L"");
				}
				else
				{
					ByteArrayInputStream bais(customPayloadPacket->data);
					DataInputStream dis(&bais);
					wstring name = dis.readUTF();
					if (name.length() <= 30)
					{
						menu->setItemName(name);
					}
				}
			}
		}
}

// 4J Added

void PlayerConnection::handleDebugOptions(shared_ptr<DebugOptionsPacket> packet)
{
	//Player player = dynamic_pointer_cast<Player>( player->shared_from_this() );
	player->SetDebugOptions(packet->m_uiVal);
}

void PlayerConnection::handleCraftItem(shared_ptr<CraftItemPacket> packet)
{
	int iRecipe = packet->recipe;

	if(iRecipe == -1)
		return;

	Recipy::INGREDIENTS_REQUIRED *pRecipeIngredientsRequired=Recipes::getInstance()->getRecipeIngredientsArray();
	shared_ptr<ItemInstance> pTempItemInst=pRecipeIngredientsRequired[iRecipe].pRecipy->assemble(nullptr);

	if(app.DebugSettingsOn() && (player->GetDebugOptions()&(1L<<eDebugSetting_CraftAnything)))
	{
		pTempItemInst->onCraftedBy(player->level, dynamic_pointer_cast<Player>( player->shared_from_this() ), pTempItemInst->count );
		if(player->inventory->add(pTempItemInst)==false )
		{
			// no room in inventory, so throw it down
			player->drop(pTempItemInst);
		}
	}
	else
	{

	
	// TODO 4J Stu - Assume at the moment that the client can work this out for us...
	//if(pRecipeIngredientsRequired[iRecipe].bCanMake) 
	//{
		pTempItemInst->onCraftedBy(player->level, dynamic_pointer_cast<Player>( player->shared_from_this() ), pTempItemInst->count );

		// and remove those resources from your inventory
		for(int i=0;i<pRecipeIngredientsRequired[iRecipe].iIngC;i++)
		{
			for(int j=0;j<pRecipeIngredientsRequired[iRecipe].iIngValA[i];j++)
			{
				shared_ptr<ItemInstance> ingItemInst = nullptr;
				// do we need to remove a specific aux value?
				if(pRecipeIngredientsRequired[iRecipe].iIngAuxValA[i]!=Recipes::ANY_AUX_VALUE)
				{
					ingItemInst = player->inventory->getResourceItem( pRecipeIngredientsRequired[iRecipe].iIngIDA[i],pRecipeIngredientsRequired[iRecipe].iIngAuxValA[i] );
					player->inventory->removeResource(pRecipeIngredientsRequired[iRecipe].iIngIDA[i],pRecipeIngredientsRequired[iRecipe].iIngAuxValA[i]);
				}
				else
				{
					ingItemInst = player->inventory->getResourceItem( pRecipeIngredientsRequired[iRecipe].iIngIDA[i] );
					player->inventory->removeResource(pRecipeIngredientsRequired[iRecipe].iIngIDA[i]);
				}

				// 4J Stu - Fix for #13097 - Bug: Milk Buckets are removed when crafting Cake
				if (ingItemInst != NULL)
				{
					if (ingItemInst->getItem()->hasCraftingRemainingItem())
					{
						// replace item with remaining result
						player->inventory->add( shared_ptr<ItemInstance>( new ItemInstance(ingItemInst->getItem()->getCraftingRemainingItem()) ) );
					}

				}
			}
		}
		
		// 4J Stu - Fix for #13119 - We should add the item after we remove the ingredients
		if(player->inventory->add(pTempItemInst)==false )
		{
			// no room in inventory, so throw it down
			player->drop(pTempItemInst);
		}

		if( pTempItemInst->id == Item::map_Id )
		{
			// 4J Stu - Maps need to have their aux value update, so the client should always be assumed to be wrong
			// This is how the Java works, as the client also incorrectly predicts the auxvalue of the mapItem
			vector<shared_ptr<ItemInstance> > items;
			for (unsigned int i = 0; i < player->containerMenu->slots->size(); i++)
			{
				items.push_back(player->containerMenu->slots->at(i)->getItem());
			}
			player->refreshContainer(player->containerMenu, &items);
		}
		else
		{
			// Do same hack as PlayerConnection::handleContainerClick does - do our broadcast of changes just now, but with a hack so it just thinks it has sent
			// things but hasn't really. This will stop the client getting a message back confirming the current inventory items, which might then arrive
			// after another local change has been made on the client and be stale.
			player->ignoreSlotUpdateHack = true;
			player->containerMenu->broadcastChanges();
			player->broadcastCarriedItem();
			player->ignoreSlotUpdateHack = false;
		}
	}

	// handle achievements
	switch(pTempItemInst->id )
	{
		case Tile::workBench_Id:		player->awardStat(GenericStats::buildWorkbench(),		GenericStats::param_buildWorkbench());		break;
		case Item::pickAxe_wood_Id:		player->awardStat(GenericStats::buildPickaxe(),			GenericStats::param_buildPickaxe());		break;
		case Tile::furnace_Id:			player->awardStat(GenericStats::buildFurnace(),			GenericStats::param_buildFurnace());		break;
		case Item::hoe_wood_Id:			player->awardStat(GenericStats::buildHoe(),				GenericStats::param_buildHoe());			break;
		case Item::bread_Id:			player->awardStat(GenericStats::makeBread(),			GenericStats::param_makeBread());			break;
		case Item::cake_Id:				player->awardStat(GenericStats::bakeCake(),				GenericStats::param_bakeCake());			break;
		case Item::pickAxe_stone_Id:	player->awardStat(GenericStats::buildBetterPickaxe(),	GenericStats::param_buildBetterPickaxe());	break;
		case Item::sword_wood_Id:		player->awardStat(GenericStats::buildSword(),			GenericStats::param_buildSword());			break;
		case Tile::dispenser_Id:		player->awardStat(GenericStats::dispenseWithThis(),		GenericStats::param_dispenseWithThis());	break;
		case Tile::enchantTable_Id:		player->awardStat(GenericStats::enchantments(),			GenericStats::param_enchantments());		break;
		case Tile::bookshelf_Id:		player->awardStat(GenericStats::bookcase(),				GenericStats::param_bookcase());			break;
	}
	//}
		// ELSE The server thinks the client was wrong...
}


void PlayerConnection::handleTradeItem(shared_ptr<TradeItemPacket> packet)
{
	if (player->containerMenu->containerId == packet->containerId)
	{
		MerchantMenu *menu = (MerchantMenu *)player->containerMenu;

		MerchantRecipeList *offers = menu->getMerchant()->getOffers(player);

		if(offers)
		{
			int selectedShopItem = packet->offer;
			if( selectedShopItem < offers->size() )
			{
				MerchantRecipe *activeRecipe = offers->at(selectedShopItem);
				if(!activeRecipe->isDeprecated())
				{
					// Do we have the ingredients?
					shared_ptr<ItemInstance> buyAItem = activeRecipe->getBuyAItem();
					shared_ptr<ItemInstance> buyBItem = activeRecipe->getBuyBItem();

					int buyAMatches = player->inventory->countMatches(buyAItem);
					int buyBMatches = player->inventory->countMatches(buyBItem);
					if( (buyAItem != NULL && buyAMatches >= buyAItem->count) && (buyBItem == NULL || buyBMatches >= buyBItem->count) )
					{
						menu->getMerchant()->notifyTrade(activeRecipe);

						// Remove the items we are purchasing with
						player->inventory->removeResources(buyAItem);
						player->inventory->removeResources(buyBItem);

						// Add the item we have purchased
						shared_ptr<ItemInstance> result = activeRecipe->getSellItem()->copy();
						
						// 4J JEV - Award itemsBought stat.
						player->awardStat(
							GenericStats::itemsBought(result->getItem()->id),
							GenericStats::param_itemsBought(
								result->getItem()->id,
								result->getAuxValue(),
								result->GetCount()
								)
							);
						
						if (!player->inventory->add(result))
						{
							player->drop(result);
						}
					}
				}
			}
		}
	}
}

INetworkPlayer *PlayerConnection::getNetworkPlayer()
{
	if( connection != NULL && connection->getSocket() != NULL) return connection->getSocket()->getPlayer();
	else return NULL;
}

bool PlayerConnection::isLocal()
{
	if( connection->getSocket() == NULL ) 
	{
		return false;
	}
	else
	{
		bool isLocal = connection->getSocket()->isLocal();
		return connection->getSocket()->isLocal();
	}
}

bool PlayerConnection::isGuest()
{
	if( connection->getSocket() == NULL ) 
	{
		return false;
	}
	else
	{
		INetworkPlayer *networkPlayer = connection->getSocket()->getPlayer();
		bool isGuest = false;
		if(networkPlayer != NULL)
		{
			isGuest = networkPlayer->IsGuest() == TRUE;
		}
		return isGuest;
	}
}















