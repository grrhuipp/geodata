// Single-file compiler for the manually maintained cnode target rules dataset.
// Build (MinGW): g++ -std=c++20 -O2 -s build.cpp -lws2_32 -o build.exe
// Usage: build.exe [data-directory] [output.dat]
// Protobuf schema (encoded directly, no protobuf runtime dependency):
// message Database { repeated Product products = 1; }
// message Product  { string name = 1; repeated Rule rules = 2; }
// message Rule     { Type type = 1; bytes value = 2; uint32 prefix = 3; }

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace fs = std::filesystem;
using Bytes = std::vector<std::byte>;

enum class Type : std::uint8_t { domain, full, keyword, regexp, ipv4, ipv6 };
struct Rule { Type type{}; std::string value; std::vector<std::string> attributes; };
struct Group { std::string product; std::vector<Rule> rules; };
struct Include { std::string product; std::vector<std::string> required; std::vector<std::string> excluded; };
struct Parsed { std::vector<Include> includes; std::vector<Rule> rules; };

static std::string trim(std::string value) {
  auto first=value.find_first_not_of(" \t\r\n"); if(first==std::string::npos)return{};
  value.erase(0,first); value.erase(value.find_last_not_of(" \t\r\n")+1); return value;
}
static std::string lower(std::string value) {
  std::ranges::transform(value,value.begin(),[](unsigned char c){return std::tolower(c);}); return value;
}
static std::string domain(std::string value) {
  value=lower(trim(std::move(value))); while(!value.empty()&&value.back()=='.')value.pop_back();
  const auto valid=[](unsigned char c){return std::isalnum(c)||c=='.'||c=='-';};
  if(value.empty()||value.size()>253||value.find("..")!=std::string::npos||!std::ranges::all_of(value,valid))throw std::runtime_error("invalid ASCII/Punycode domain: "+value);
  std::stringstream labels(value); for(std::string label;std::getline(labels,label,'.');)if(label.size()>63||label.front()=='-'||label.back()=='-')throw std::runtime_error("invalid domain label: "+value);
  return value;
}
static std::string cidr(std::string_view input,bool need_v6=false) {
  auto slash=input.find('/'); auto address=std::string(input.substr(0,slash)); bool v6=address.find(':')!=std::string::npos;
  if(need_v6&&!v6)throw std::runtime_error("ip6 requires IPv6: "+std::string(input)); int maximum=v6?128:32,prefix=maximum;
  if(slash!=std::string_view::npos){auto part=input.substr(slash+1);auto[end,ec]=std::from_chars(part.data(),part.data()+part.size(),prefix);if(ec!=std::errc{}||end!=part.data()+part.size()||prefix<0||prefix>maximum)throw std::runtime_error("invalid CIDR prefix: "+std::string(input));}
  std::array<std::uint8_t,16> bytes{}; int family=v6?AF_INET6:AF_INET;
  if(inet_pton(family,address.c_str(),bytes.data())!=1)throw std::runtime_error("invalid IP: "+address);
  for(int bit=prefix;bit<maximum;++bit)bytes[bit/8]&=static_cast<std::uint8_t>(~(1u<<(7-bit%8)));
  char text[INET6_ADDRSTRLEN]{}; inet_ntop(family,bytes.data(),text,sizeof(text)); return std::string(text)+"/"+std::to_string(prefix);
}

static Parsed parse_file(const fs::path& path,const std::string& group) {
  std::ifstream in(path); if(!in)throw std::runtime_error("cannot open "+path.string()); Parsed parsed; std::size_t number=0;
  for(std::string line;std::getline(in,line);){++number;if(auto hash=line.find('#');hash!=std::string::npos)line.resize(hash);line=trim(std::move(line));if(line.empty())continue;
    if(line.starts_with('@'))throw std::runtime_error(path.string()+":"+std::to_string(number)+": metadata is forbidden; filename is the product name");
    auto space=line.find_first_of(" \t");auto token=line.substr(0,space);auto colon=token.find(':');auto kind=colon==std::string::npos?"domain":lower(token.substr(0,colon));auto value=colon==std::string::npos?token:token.substr(colon+1);
    std::vector<std::string> options;if(space!=std::string::npos){std::stringstream stream(line.substr(space+1));for(std::string option;stream>>option;)options.push_back(option);}
    if(kind=="include"){Include include{lower(value)};if(include.product.empty())throw std::runtime_error(path.string()+":"+std::to_string(number)+": empty include");for(auto option:options){if(!option.starts_with('@'))throw std::runtime_error(path.string()+":"+std::to_string(number)+": invalid include option "+option);option=lower(option.substr(1));if(option.starts_with('-'))include.excluded.push_back(option.substr(1));else include.required.push_back(option);}parsed.includes.push_back(std::move(include));continue;}Rule rule;
    if(kind=="domain"){rule.type=Type::domain;rule.value=domain(value);}else if(kind=="full"){rule.type=Type::full;rule.value=domain(value);}else if(kind=="keyword"){rule.type=Type::keyword;if(value.empty())throw std::runtime_error(path.string()+":"+std::to_string(number)+": empty keyword");rule.value=lower(value);}else if(kind=="regexp"){rule.type=Type::regexp;if(value.empty())throw std::runtime_error(path.string()+":"+std::to_string(number)+": empty regexp");rule.value=value;}
    else if(kind=="ip"){rule.type=Type::ipv4;rule.value=cidr(value);if(rule.value.find(':')!=std::string::npos)throw std::runtime_error(path.string()+":"+std::to_string(number)+": ip requires IPv4");}else if(kind=="ip6"){rule.type=Type::ipv6;rule.value=cidr(value,true);}else throw std::runtime_error(path.string()+":"+std::to_string(number)+": unknown type "+kind);
    for(auto option:options){if(!option.starts_with('@'))throw std::runtime_error(path.string()+":"+std::to_string(number)+": invalid rule option "+option);rule.attributes.push_back(lower(option.substr(1)));}std::ranges::sort(rule.attributes);rule.attributes.erase(std::unique(rule.attributes.begin(),rule.attributes.end()),rule.attributes.end());
    parsed.rules.push_back(std::move(rule));
  }return parsed;
}

static std::vector<Rule> polish(std::vector<Rule> rules){
  std::ranges::sort(rules,[](const Rule&a,const Rule&b){return std::tie(a.type,a.value,a.attributes)<std::tie(b.type,b.value,b.attributes);});
  rules.erase(std::unique(rules.begin(),rules.end(),[](const Rule&a,const Rule&b){return a.type==b.type&&a.value==b.value&&a.attributes==b.attributes;}),rules.end());
  std::unordered_set<std::string> domains;for(const auto&rule:rules)if(rule.type==Type::domain)domains.insert(rule.value);
  std::vector<Rule> result;result.reserve(rules.size());
  for(auto&rule:rules){if((rule.type!=Type::domain&&rule.type!=Type::full)||!rule.attributes.empty()){result.push_back(std::move(rule));continue;}auto current=rule.type==Type::full?"."+rule.value:rule.value;bool redundant=false;while(true){auto dot=current.find('.');if(dot==std::string::npos)break;current=current.substr(dot+1);if(domains.contains(current)){redundant=true;break;}}if(!redundant)result.push_back(std::move(rule));}
  std::ranges::sort(result,[](const Rule&a,const Rule&b){return std::tie(a.type,a.value,a.attributes)<std::tie(b.type,b.value,b.attributes);});return result;
}

static std::vector<Group> load(const fs::path& directory) {
  if(!fs::is_directory(directory))throw std::runtime_error("data directory not found: "+directory.string());std::map<std::string,Parsed> files;
  for(const auto& entry:fs::directory_iterator(directory))if(entry.is_regular_file()){auto id=lower(entry.path().filename().string());files.emplace(id,parse_file(entry.path(),id));}
  std::unordered_map<std::string,std::vector<Rule>> cache;
  std::function<std::vector<Rule>(const std::string&,std::vector<std::string>)> expand;
  expand=[&](const std::string& product,std::vector<std::string> stack){if(auto cached=cache.find(product);cached!=cache.end())return cached->second;if(std::ranges::find(stack,product)!=stack.end()){std::string chain;for(const auto&item:stack)chain+=item+" -> ";throw std::runtime_error("include cycle: "+chain+product);}auto found=files.find(product);if(found==files.end())throw std::runtime_error("included product not found: "+product);stack.push_back(product);auto rules=found->second.rules;for(const auto&included:found->second.includes){auto added=expand(included.product,stack);for(auto&rule:added){const auto has=[&](const std::string& attr){return std::ranges::find(rule.attributes,attr)!=rule.attributes.end();};if(std::ranges::all_of(included.required,has)&&std::ranges::none_of(included.excluded,has))rules.push_back(std::move(rule));}}rules=polish(std::move(rules));cache.emplace(product,rules);return rules;};
  std::vector<Group> groups;for(const auto&[product,file]:files){if(product.empty())throw std::runtime_error("empty product filename");auto rules=expand(product,{});std::ranges::sort(rules,[](const Rule&a,const Rule&b){return std::tie(a.type,a.value)<std::tie(b.type,b.value);});rules.erase(std::unique(rules.begin(),rules.end(),[](const Rule&a,const Rule&b){return a.type==b.type&&a.value==b.value;}),rules.end());if(rules.empty())throw std::runtime_error(product+": empty product");groups.push_back({product,std::move(rules)});}return groups;
}

static void varint(Bytes& out,std::uint64_t value){while(value>=128){out.push_back(static_cast<std::byte>((value&127)|128));value>>=7;}out.push_back(static_cast<std::byte>(value));}
static void field_varint(Bytes& out,std::uint32_t field,std::uint64_t value){varint(out,static_cast<std::uint64_t>(field)<<3);varint(out,value);}
static void field_bytes(Bytes& out,std::uint32_t field,std::span<const std::byte> value){varint(out,(static_cast<std::uint64_t>(field)<<3)|2);varint(out,value.size());out.insert(out.end(),value.begin(),value.end());}
static void field_text(Bytes& out,std::uint32_t field,std::string_view value){field_bytes(out,field,std::as_bytes(std::span(value)));}
static Bytes encode_rule(const Rule& rule){Bytes out;field_varint(out,1,static_cast<std::uint8_t>(rule.type));if(rule.type==Type::ipv4||rule.type==Type::ipv6){auto slash=rule.value.find('/');auto address=rule.value.substr(0,slash);std::array<std::byte,16>packed{};bool v6=rule.type==Type::ipv6;if(inet_pton(v6?AF_INET6:AF_INET,address.c_str(),packed.data())!=1)throw std::runtime_error("invalid normalized IP");field_bytes(out,2,std::span(packed).first(v6?16:4));field_varint(out,3,std::stoul(rule.value.substr(slash+1)));}else field_text(out,2,rule.value);return out;}
static Bytes compile(const std::vector<Group>&groups){Bytes database;for(const auto&group:groups){Bytes product;field_text(product,1,group.product);for(const auto&rule:group.rules){auto encoded=encode_rule(rule);field_bytes(product,2,encoded);}field_bytes(database,1,product);}return database;}

static void write_dat(const fs::path&path,std::span<const std::byte>data){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));if(!file)throw std::runtime_error("cannot write "+path.string());std::cout<<"built protobuf "<<path<<": "<<data.size()<<" bytes\n";}

int main(int argc,char**argv){try{fs::path data=argc>1?argv[1]:"data",output=argc>2?argv[2]:"geodata.dat";auto groups=load(data);auto binary=compile(groups);write_dat(output,binary);if(argc<=2){fs::remove("cnode-targets.dat");fs::remove("target-rules.dat");fs::remove("target-rules.cdb.xz");fs::remove("target-rules.pb.xz");fs::remove("target-rules.check.cdb.xz");}std::size_t rules=0;for(const auto&g:groups)rules+=g.rules.size();std::cout<<groups.size()<<" products, "<<rules<<" rules\n";return 0;}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}}
