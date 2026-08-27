🔴 New finding: InventoryDbusCodec is compiled only into the daemon — inventory-client is broken for consumers

InventoryClient.cpp calls InventoryDbusCodec::decodeSnapshot/decodeFields/decodeSourceStates, but InventoryDbusCodec.cpp lives in publishers/ and is only in the inventory-agentd target. A static lib doesn't need to resolve at archive time, so this won't fail at build — it fails when any consumer links libinventory-client.a:


undefined reference to RSCGroup::InventoryDbusCodec::decodeSnapshot(...)

Two fixes, in preference order:



Move InventoryDbusCodec.{h,cpp} into fabric-contracts/ (or a small libs/inventory-codec). Your libs README literally reserves fabric-contracts for "shared wire-format conversion helpers" — the codec is exactly that. Daemon and client both link it; layering stays honest.

Pragmatic shortcut: add ${PUBLISHERS_BASE}/InventoryDbusCodec.cpp to the inventory-client target. Works today; minor smell (client reaching into daemon-land), fine as a stopgap if you don't want the move now.


If you go with (1), also add to the client target so dependencies propagate:


target_link_libraries(inventory-client PUBLIC DBusCxx::DBusCxx SigCxx::SigCxx glog)

(PUBLIC so consumers of the static lib get the transitive deps automatically.)


🟡 Residual trivia (30 seconds each)


Leftover commented block in DbusInventoryTransport.cpp — the raw-uint32_t RequestNameResponse version is still there as a comment. You resolved the enum (PrimaryOwner/AlreadyOwner); delete the dead comment before it rots.

<cstring> in WatchedFileSource.cpp — still unused.

Settings(){}; in InventoryService.h — the user-provided default ctor makes Settings a non-aggregate; harmless as used, but if you don't need it, drop it and let the brace-init defaults stand alone.
