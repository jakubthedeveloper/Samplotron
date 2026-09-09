#include <unity.h>
#include <vector>
#include <cstring>
#include "../support/arduino_stubs.cpp"
#include "../../src/wav_validation.cpp"
#include "../../src/validated_wav_source.cpp"
#include "../../src/sample_library.cpp"
#include "../../src/sample_classifier.cpp"
using Bytes = std::vector<uint8_t>;
using Status = WavValidation::Status;
void set32(Bytes &b, size_t p, uint32_t v) { for (int i=0;i<4;++i) b[p+i]=v>>(8*i); }
Bytes fixture() { Bytes b(52, 0x37); WavValidation::pcmHeader(b.data(),8); return b; }
void resizeRiff(Bytes &b) { set32(b,4,b.size()-8); }
void insertChunk(Bytes &b, size_t pos, const char *name, Bytes payload) {
  Bytes c(8); std::memcpy(c.data(),name,4); set32(c,4,payload.size());
  c.insert(c.end(),payload.begin(),payload.end()); if(payload.size()%2) c.push_back(0);
  b.insert(b.begin()+pos,c.begin(),c.end()); resizeRiff(b);
}
struct Reader : WavValidation::Reader {
  Bytes bytes; int failAt=-1; std::vector<std::pair<uint32_t,size_t>> reads;
  explicit Reader(Bytes b):bytes(b){}
  uint32_t size() const override{return bytes.size();}
  bool readAt(uint32_t off,uint8_t *dst,size_t n) override {
    reads.push_back({off,n}); if(int(off)==failAt || off>bytes.size() || n>bytes.size()-off)return false;
    std::memcpy(dst,bytes.data()+off,n); return true;
  }
};
void expect(Bytes b, Status status) { Reader r(b); TEST_ASSERT_EQUAL_INT(int(status),int(WavValidation::validate(r).status)); }
void test_valid_headers_skip_pcm_and_odd_metadata() {
  Bytes b=fixture(); insertChunk(b,12,"JUNK",{1,2,3}); insertChunk(b,b.size(),"LIST",{4});
  Reader r(b); auto info=WavValidation::validate(r);
  TEST_ASSERT_TRUE(info.playable()); TEST_ASSERT_EQUAL_UINT32(56,info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(8,info.dataBytes);
  for(auto range:r.reads) TEST_ASSERT_TRUE(range.first+range.second<=56 || range.first>=64);
  b=fixture(); b.insert(b.begin()+36,2,0); set32(b,16,18); resizeRiff(b); expect(b,Status::Valid);
}
void test_unsupported_formats() {
  for(auto change:std::vector<std::pair<int,int>>{{20,3},{20,0xfe},{22,2},{24,0},{34,24},{34,8}}) {
    auto b=fixture(); b[change.first]=change.second; expect(b,Status::Unsupported);
  }
}
void test_invalid_sizes_and_format_fields() {
  auto original=fixture();
  for(auto change:std::vector<std::pair<int,int>>{{0,'X'},{8,'X'},{4,0},{28,0},{32,4},{40,0},{40,7},{16,17}}) {
    auto b=original; b[change.first]=change.second; expect(b,Status::Invalid);
  }
  auto b=original; set32(b,40,0xffffffff); expect(b,Status::Invalid);
  b=original; b.pop_back(); expect(b,Status::Invalid);
  b=original; b.push_back(0); expect(b,Status::Invalid);
  resizeRiff(b); expect(b,Status::Invalid);
  b=original; b.insert(b.begin()+36,2,1); set32(b,16,18); resizeRiff(b); expect(b,Status::Invalid);
}
void test_missing_duplicate_and_out_of_order_chunks() {
  auto b=fixture(); std::memcpy(b.data()+36,"JUNK",4); expect(b,Status::Invalid);
  b=fixture(); std::memcpy(b.data()+12,"JUNK",4); expect(b,Status::Invalid);
  b=fixture(); insertChunk(b,b.size(),"data",{0,0}); expect(b,Status::Invalid);
  b=fixture(); Bytes fmt(b.begin()+20,b.begin()+36); insertChunk(b,36,"fmt ",fmt); expect(b,Status::Invalid);
  b=fixture(); insertChunk(b,12,"data",{0,0}); expect(b,Status::Invalid);
  b=fixture(); insertChunk(b,b.size(),"JUNK",{1}); b.pop_back(); resizeRiff(b); expect(b,Status::Invalid);
}
void test_read_failure_is_not_playable() {
  for(int off:{0,12,20,36}) { Reader r(fixture()); r.failAt=off; auto result=WavValidation::validate(r);
    TEST_ASSERT_EQUAL_INT(int(Status::ReadError),int(result.status)); TEST_ASSERT_FALSE(result.playable()); }
}
void test_catalog_validates_unassigned_and_classifier_uses_only_cache() {
  FakeSD::files.clear(); FakeSD::files["/samples/z.wav"]=fixture();
  auto bad=fixture(); bad[22]=2; FakeSD::files["/samples/a.wav"]=bad;
  FakeSD::files["/samples/ignored.txt"]=fixture(); FakeSD::bytesRead=0;
  SampleLibrary::Catalog catalog; int callbacks=0;
  SampleLibrary::loadFromSd(catalog,[](void *p){++*static_cast<int*>(p);},&callbacks);
  TEST_ASSERT_EQUAL_INT(2,catalog.checkedCount); TEST_ASSERT_EQUAL_INT(1,catalog.rejectedCount);
  TEST_ASSERT_EQUAL_INT(3,callbacks); TEST_ASSERT_EQUAL_STRING("a.wav",catalog.names[0].c_str());
  TEST_ASSERT_FALSE(catalog.playable(0)); TEST_ASSERT_TRUE(catalog.playable(1));
  TEST_ASSERT_EQUAL_UINT32(88,FakeSD::bytesRead);
  SettingsStore::SamplerSettings settings; settings.assignmentCount=4; settings.sampleRamBudgetBytes=8;
  settings.assignments[0].samplePath="/samples/z.wav"; settings.assignments[1].samplePath="/samples/z.wav";
  settings.assignments[2].samplePath="/samples/a.wav"; settings.assignments[3].samplePath="/samples/missing.wav";
  FakeSD::files.clear(); int opens=FakeSD::opens, reads=FakeSD::reads;
  SampleClassifier::ClassificationReport report;
  SampleClassifier::classifyAssignedSamples(settings,catalog,report);
  TEST_ASSERT_EQUAL_INT(2,report.ramSampleCount); TEST_ASSERT_EQUAL_UINT32(8,report.sampleRamUsedBytes);
  TEST_ASSERT_EQUAL_UINT32(44,report.items[1].dataOffset); TEST_ASSERT_EQUAL_INT(1,report.invalidFormatCount);
  TEST_ASSERT_EQUAL_INT(1,report.missingFileCount);
  settings.sampleRamBudgetBytes=0; SampleClassifier::classifyAssignedSamples(settings,catalog,report);
  TEST_ASSERT_EQUAL_INT(2,report.streamSampleCount);
  TEST_ASSERT_EQUAL_INT(opens,FakeSD::opens); TEST_ASSERT_EQUAL_INT(reads,FakeSD::reads);
}
struct Source : AudioFileSource {
  Bytes bytes; uint32_t pos=0; int reads=0; bool opened=true;
  explicit Source(Bytes b):bytes(b){}
  bool isOpen() override{return opened;}
  uint32_t getSize() override{return bytes.size();}
  uint32_t read(void *dst,uint32_t n) override {++reads; n=std::min(n,uint32_t(bytes.size()-pos)); std::memcpy(dst,bytes.data()+pos,n);pos+=n;return n;}
  bool seek(int32_t p,int dir) override {if(dir!=SEEK_SET || p<0 || size_t(p)>bytes.size())return false;pos=p;return true;}
  bool close() override{opened=false;return true;}
};
void test_stream_uses_virtual_header_and_cached_data_bounds() {
  auto b=fixture(); insertChunk(b,36,"JUNK",{1,2,3}); insertChunk(b,b.size(),"LIST",{9});
  Reader reader(b); auto info=WavValidation::validate(reader); Source source(b); ValidatedWavSource view;
  TEST_ASSERT_TRUE(view.attach(&source,info)); uint8_t out[100]={};
  TEST_ASSERT_EQUAL_UINT32(44,view.read(out,44)); TEST_ASSERT_EQUAL_INT(0,source.reads);
  auto canonical=fixture(); TEST_ASSERT_EQUAL_MEMORY(canonical.data(),out,44);
  TEST_ASSERT_EQUAL_UINT32(8,view.read(out,100)); TEST_ASSERT_EQUAL_MEMORY(b.data()+info.dataOffset,out,8);
  TEST_ASSERT_EQUAL_UINT32(0,view.read(out,1)); TEST_ASSERT_TRUE(view.seek(-2,SEEK_END));
  TEST_ASSERT_EQUAL_UINT32(2,view.read(out,8)); TEST_ASSERT_FALSE(view.seek(1,SEEK_END));
  TEST_ASSERT_FALSE(view.seek(-1,SEEK_SET)); TEST_ASSERT_TRUE(view.seek(40,SEEK_SET));
  TEST_ASSERT_EQUAL_UINT32(12,view.read(out,100)); TEST_ASSERT_EQUAL_MEMORY(canonical.data()+40,out,12);
  TEST_ASSERT_TRUE(view.close()); TEST_ASSERT_FALSE(source.isOpen());
}
void test_stream_rejects_unchecked_and_truncated_files() {
  Source source(fixture()); ValidatedWavSource view; WavValidation::Result info;
  TEST_ASSERT_FALSE(view.attach(&source,info));
  Reader reader(fixture()); info=WavValidation::validate(reader); source.bytes.pop_back();
  TEST_ASSERT_FALSE(view.attach(&source,info)); TEST_ASSERT_FALSE(view.isOpen());
}
void setUp(){} void tearDown(){}
int main(){UNITY_BEGIN();
 RUN_TEST(test_valid_headers_skip_pcm_and_odd_metadata);
 RUN_TEST(test_unsupported_formats); RUN_TEST(test_invalid_sizes_and_format_fields);
 RUN_TEST(test_missing_duplicate_and_out_of_order_chunks); RUN_TEST(test_read_failure_is_not_playable);
 RUN_TEST(test_catalog_validates_unassigned_and_classifier_uses_only_cache);
 RUN_TEST(test_stream_uses_virtual_header_and_cached_data_bounds);
 RUN_TEST(test_stream_rejects_unchecked_and_truncated_files);
 return UNITY_END();}
