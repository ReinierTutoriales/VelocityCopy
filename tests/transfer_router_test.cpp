#include "velocitycopy/transfer_router.hpp"
using namespace velocitycopy;
int main(){
 if(fallback_volume_key(L"\\\\Server\\Share\\folder\\file.bin")!=L"\\\\server\\share")return 22;
 if(fallback_volume_key(L"Z:\\folder\\file.bin")!=L"z:\\")return 23;
 if(!same_device({fallback_volume_key(L"\\\\SERVER\\SHARE\\a"),{}},{fallback_volume_key(L"\\\\server\\share\\b"),{}}))return 24;
 const auto local=resolve_storage_key(std::filesystem::current_path());
 if(local.volume.empty())return 20;
 if(local.volume.rfind(L"\\\\?\\Volume{",0)!=0)return 21;
 TransferRequest r{L"D:\\out",FileOperation::Copy,{L"D:\\",1},{L"C:\\",0}};
 if(route_transfer(r,{}).decision!=RouteDecision::StartNew)return 1;
 ActiveSession s{7,L"D:\\out",FileOperation::Copy,true,{L"D:\\",1},{L"C:\\",0}};
 ActiveSession one[]={s}; auto q=route_transfer(r,one); if(q.decision!=RouteDecision::Ask||q.offered.size()!=2||q.offered[0]!=RouteChoice::Append||q.recommended!=RouteChoice::Append)return 2;
 RoutePreferences ap{RouteChoice::Append,{}}; q=route_transfer(r,one,ap); if(q.decision!=RouteDecision::AppendTo||q.window_id!=7)return 3;
 s.accepting_appends=false; one[0]=s; q=route_transfer(r,one); if(q.decision!=RouteDecision::Ask||q.offered[0]!=RouteChoice::Wait)return 4;
 r.operation=FileOperation::Move; q=route_transfer(r,one); if(q.decision!=RouteDecision::Ask||q.offered[0]!=RouteChoice::Wait)return 5;
 r={L"D:\\other",FileOperation::Copy,{L"D:\\",1},{L"X:\\",{}}}; q=route_transfer(r,one); if(q.decision!=RouteDecision::Ask)return 6;
 r.destination={L"E:\\",1}; r.destination_root=L"E:\\out"; q=route_transfer(r,one); if(q.decision!=RouteDecision::Ask)return 7;
 r.destination={L"E:\\",{}}; r.source={L"X:\\",{}}; q=route_transfer(r,one); if(q.decision!=RouteDecision::StartNew)return 8;
 r.source={L"C:\\",{}}; one[0].source={L"C:\\",{}}; q=route_transfer(r,one); if(q.decision!=RouteDecision::Ask)return 9;
 ActiveSession many[]={{3,L"Z:\\x",FileOperation::Copy,true,{L"Z:\\",9},{L"Y:\\",8}},{7,L"E:\\out",FileOperation::Copy,true,{L"E:\\",{}},{L"C:\\",{}}}}; q=route_transfer(r,many); if(q.window_id!=7)return 10;
 ActiveSession tie[]={{9,L"D:\\a",FileOperation::Copy,true,{L"D:\\",1},{L"C:\\",0}},{4,L"D:\\b",FileOperation::Copy,true,{L"D:\\",1},{L"C:\\",0}}};
 TransferRequest t{L"D:\\c",FileOperation::Copy,{L"D:\\",1},{L"X:\\",{}}}; q=route_transfer(t,tie); if(q.window_id!=4)return 11;
 ActiveSession prio[]={{4,L"D:\\b",FileOperation::Copy,true,{L"D:\\",1},{L"C:\\",0}},{9,L"D:\\c",FileOperation::Copy,true,{L"D:\\",1},{L"C:\\",0}}};
 auto p=route_transfer(t,prio); if(p.decision!=RouteDecision::Ask||p.offered.empty()||p.offered[0]!=RouteChoice::Append||p.window_id!=9)return 12;
 RoutePreferences parallel{{},RouteChoice::Parallel}; p=route_transfer(t,tie,parallel); if(p.decision!=RouteDecision::StartNew||p.window_id!=0)return 13;
 return 0;
}
