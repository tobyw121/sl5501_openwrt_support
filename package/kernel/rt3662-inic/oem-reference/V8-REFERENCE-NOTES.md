# v8 reference notes

Primary behavior reference for the SL5501 boot path is the unstripped Sphairon
`rtXXXX_iNIC.ko` host driver 2.7.0.5. The readable Ralink 2.4.0.9.10 GPL source in
this directory is used to name/understand equivalent paths where the 2.7.0.5
binary has only symbols/disassembly.

Critical functions compared for v8:

- `mii_send_packet()` / `rlk_inic_mii_xmit()`
- `SendRaCfgCommand()`
- `_upload_profile()` / `upload_timeout()`
- `RaCfgInitCfgAction()`
- `_upload_firmware()` / `RaCfgUploadAction()`
- `RaCfgSetUp()` / `RaCfgWaitSyncRsp()`
- `DispatchAdapter()`
- `RaCfgRestartiNIC()`
- `RaCfgConcurrentOpenAction()` / `ConcurrentCardInfoRead()`

Where old generic source and the SL5501 2.7.0.5 binary differ, v8 follows the
SL5501 binary (notably no added 30-ms sleep between successful firmware chunks).
