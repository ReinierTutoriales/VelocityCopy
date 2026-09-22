#include "velocitycopy/transfer_router.hpp"
using namespace velocitycopy;
int main(){
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
 return 0;
}
