#include <memory>
#include <string>

#include "envoy/extensions/filters/http/oauth2/v3/oauth.pb.h"
#include "envoy/extensions/filters/http/oauth2/v3/oauth.pb.validate.h"
#include "envoy/http/async_client.h"
#include "envoy/http/message.h"

#include "source/common/common/macros.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/common/secret/secret_manager_impl.h"
#include "source/extensions/filters/http/oauth2/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

static const std::string TEST_CALLBACK = "/_oauth";
static const std::string TEST_CLIENT_ID = "1";
static const std::string TEST_CLIENT_SECRET_ID = "MyClientSecretKnoxID";
static const std::string TEST_TOKEN_SECRET_ID = "MyTokenSecretKnoxID";
static const std::string TEST_DEFAULT_SCOPE = "user";
static const std::string TEST_ENCODED_AUTH_SCOPES = "user%20openid%20email";
static const std::string TEST_CSRF_TOKEN =
    "00000000075bcd15.na6kru4x1pHgocSIeU/mdtHYn58Gh1bqweS4XXoiqVg=";
// {"url":"https://traffic.example.com/original_path?var1=1&var2=2","csrf_token":"${extracted}"}
static const std::string TEST_ENCODED_STATE =
    "eyJ1cmwiOiJodHRwczovL3RyYWZmaWMuZXhhbXBsZS5jb20vb3JpZ2luYWxfcGF0aD92YXIxPTEmdmFyMj0yIiwiY3NyZl"
    "90b2tlbiI6IjAwMDAwMDAwMDc1YmNkMTUubmE2a3J1NHgxcEhnb2NTSWVVL21kdEhZbjU4R2gxYnF3ZVM0WFhvaXFWZz0i"
    "fQ";
static const std::string TEST_CODE_VERIFIER = "Fc1bBwAAAAAVzVsHAAAAABXNWwcAAAAAFc1bBwAAAAA";
static const std::string TEST_ENCRYPTED_CODE_VERIFIER =
    "Fc1bBwAAAAAVzVsHAAAAABjf6i_Hvf8T2dEuEhPhhDNMlp16az-0dxisL-TzJKaZjOMF8nov_pG377FHmpKcsA";
static const std::string TEST_CODE_CHALLENGE = "YRQaBq_UpkWzfr6JvtNnh7LMfmPVcIKVYdV98ugwmLY";
static const std::string TEST_ENCRYPTED_ACCESS_TOKEN =
    "Fc1bBwAAAAAVzVsHAAAAAHDCo6XWwdgw5IYsxjfymIQ"; //"access_code"
static const std::string TEST_ENCRYPTED_ID_TOKEN =
    "Fc1bBwAAAAAVzVsHAAAAAJohQ-XDfnYLdgIQ2yJfRZQ"; //"some-id-token"
static const std::string TEST_ENCRYPTED_REFRESH_TOKEN =
    "Fc1bBwAAAAAVzVsHAAAAAERBBlyQ3ASXvDHzyIRDhLwvl1w07AKhjwBz1s4wJGX8"; //"some-refresh-token"
static const std::string TEST_HMAC_SECRET = "asdf_token_secret_fdsa";
static const std::string TEST_ENCRYPTED_ACCESS_TOKEN_CHUNK_1 =
    "Fc1bBwAAAAAVzVsHAAAAAD_ypXMASeUN_fjH-g9bL24jvDxGz0pe5AjExfk4zlZlzFb4V0a3mRT4SAVliOpQ3g_"
    "5r9tIyI96E-bK_XCz3xyGI5owpJzWvpE1sQKQqytXZrH_"
    "xbczkOcZUI0GShSaPLT2BP6yb3wBiXCvprBdUalAvwi1oJYuehlM8zYLkxYTg5T8z5gEN_"
    "y2GWpv8AbWMha0buEUvrFbEeiPtHK8-eIjjjIKT3it8nTkczTCb-wSVWiEqe8xd6-2y3BuX7p8O5_fJm9B6dw-G-SF_X_"
    "iE3HZ42jxdVE6IIdoIXKnOvP9vULgdw0X-aEePiAqTR3TJncsPXN8dmvtLEfk1_wh6p-"
    "ksK8LWuNHAU1zl3burFwxo07rdDV3-4ERbLwdi6TCAC2pwjkY_1zcb0comAVrH4VHTM7Xug_JaUL1F0tVZQbm8S4_"
    "0NuoXcd7dJtXDaNdzsjl0vZdYY6UDkugn7nJoMKs_uLd8QrZ2TVZ48HPOP2PBJko4TRZxfRgC01M_"
    "U6JmAlyp61TewWv4lq1ZCRVO6qurpGSusdVi38pzo5gXl4h9dfN_0CZFHFsnRfMDL7q1SXOpo4qQ48Eoak8bhrc6de_"
    "sCXQbwAEfOCyYODFPkZkbGYWtB3mgrywK-DeytW0_-9_huwLxGaTaINycYs0MnQA-78zqdEp7_XimuP_"
    "cC7iNbsuUFISiI7wF52mbAE5AgYU8Rv_9Kiac70EqjZXKdwzM_t5Ob_O9h6RVL0LLNHDmERYapuJoA-"
    "CgvtEWl17CCfjzUaMpVG0AZAUB9b8n1W3cZ3tFmtGegrAzb-"
    "ZAdjiGcjIjrNhEVCBbhS3pRjFoVlErIelTUMyDmKWAGeBv5rhVu912qImWALVlDxwHFUKTtoEfloqI57h2CDwwlX2rJtb0"
    "7UeQHHxgHSRUH6ENsLdqOsbZRj4VgXm9sx53QFJDme-4HjQsm7hogfdS1zsyP9kojcwJjcbDw7Yv8jbd9PgxKkPXLO-"
    "6ST-oCLRuoosIFJDIsPjDxNs_"
    "ne1cvN2ySheVIEmmJB85moRiJ9hrGDqNjowqlaAshcV9vYcIp7qyaiQJtOJF0Nw8SQk4jV8Uq93Zud3DqjH4dOaLCGSMOO"
    "dL1SUMpIP4ijXK90Hl_kuKiNOWJ_Wfn4zCSzc4k5DO09RtGmor05SlQYKQ38QEEo8GmT7I2_"
    "7J54bYTVmJXoslcQ2Uo9bfvKFF1a0OWYX3raeEZWgcrjjTF-"
    "amAbmEi5ozdYiP3xTaDmJfbTd8sfX1hl0xLYYMUbBhr6eEhSzhxiF82IuaZ_MlWXcFbx0ev0FDM6-Wg0UiytYs__"
    "uRHYKmMY8NEwjQ-c4MbcX0Pk3GzUWwr7Et2rAc4pZmhNIS-NI-5RfYHBVvhJRoTPB6Dk-"
    "8kZjpYt7D5XMgi8pZwOJ9Q5kJlzE3b0pEarqg1wHmBHZMv0JC3XUtAVAeGsuEkV0tvdYGFzCUmFhA8FsfOju-"
    "WUtX1NB1tAoy8L_zEbnDQrxHSqlNyPCituwELxPefWE1WfR0FzKQWpuOF9t0JRnYGtTtOZ3tLdPHh9-"
    "aDsXFbUC3eclz4jCcuTVtaZethJTcL-0h2iBH8t9B7_SlJ_xPMptNHmUG3SWTR35GAmA0X5p_IYNo-4nkhT-"
    "6NU4q1ibys9Qp26s3AENUDaNOgoaHuypPauT0pQ21AbF1ixVTXZVi_cCJ1ZZe9_"
    "JvuIVWjKxVlTiKVfHgieWo2SPIYVE5RWSpKhWRyt1se_sYW-4CBWnLEa_"
    "CVxWSQysxzanez1qi56Inv0eToGTXBOyhr7d8LHUCYZ_N2k2Q1n4VDZ6gC4Yqiow5uTRSt5in_"
    "IqoBl6356dZ8K9X1XkslBDa3xwskRFcb-"
    "Qf0wR6zKI6HMlOiLeg0IgcqxibCq2c7ZXJdxUWRjNrMbMieUcUrfCAKqBA7eZfI5d3-"
    "frrAbCCIY1x6xKEOqoUESzlLp9sMPVtiL2SsaHupPiTiQLsV1zevCvmmgLokTMLvp91PjdGy88g78QrH5oaj6NxH1JryK0"
    "en0HCzwYsgKcXgXdPjsQq4w6BbhcPNHitxdjgQlXEqFVe2K7RMp-1dCCzOmYVafZC4J6QuI8NMje-USyfaeo_"
    "LycufFJhdrnSv5O534UhPugHwh6aUPBaSbIZ9kJOTqghoPrVYRU0gQqCUcDo0_"
    "CzptSdIvLwc5vKS56uEhGQxPt7gY8DVQmj9zlWJSK5lGBwwpglit5SOkQELKyg41flYGvcvIyM5Z20vgaXg_"
    "Jsu5VAk4c2aQH-cuHPYqyoojcq-EaVMY5TMpJv5cCgsMM0MImQOtZyCsTNAS2-mn7In6vKpV-"
    "Y6nWf89cSRlj2AQevisBziOgZo2Fs2fosRebHpDkhBXSGVfg5c3A5t0D1cOoCyfLMwR0A7re2kPfkBHurbp61zOr7UWELm"
    "wqeEc3joYAcOysLuYqVid3CB7STAMQr9zTgaXajWMrfXE-Vp1KhjXWNM1ESH7K4Qqhc7Dk9Fqa3QDvLQ_"
    "DlGkMwTHsL8i1TRejgcGQzWY3i-"
    "G1hMKJa9oYip0EbJSuKLlHvOmP7DqXvp8kgkj2rPggEr8Ca5XezNRzn1pv255uxjbmjqdrr8hOA2-bBi2-xB4fUAFu_"
    "dX7pfvBqES7cildhYXyeDKd-ck0KmMCU1Bx9wttgKZvHqqKh1S6n4cUeNZVBsib4S0S_IxWRCYK3czGi_"
    "Dg61dOWAvK8VMOrWudWYQ6mY48m5nf2tpFetcQQmSbodfiSMVFsqK-R7O0eO62sYh-q1ycCaZ-"
    "eqJliBZFqkUuQjpx2nRvfhk-HgZhZkx1IQVxFpSK28sKf7_8JFYJuHnABUGYNHK7A0Jg86eklOZa0m-HmFb4qaRIUkR_"
    "SRN9-czOOh6zmqhAb9dnEznx6YauYajlfOHDxxNQ9Bisimc8jIHoklwvc10eCnJrL0rbOG2pvGlzTtMg48vD_lS0__"
    "9GLoRDGdkEwEMJ2D3RKL-YblQ6ieA_ypyC2wWOx1woMM9ruvzIKUlcS9tfGvr3n0j_"
    "sO7dpfuyw1B1ul09Vpf7CJvqhoY7Xd4iWz1efOvSAkeVN4Mi3hywKbUGjXxLdhszDMFeudtxbrWhtAOik8xNgDwHg8tXNA"
    "bRQ9j6tSz-3uvW7UAU_Dnb6FVIvDxBfTnVwMhk09vbvY4WNfi5JsB_"
    "etSDvnnaroR9TIFf8TbHmS9xPI4QjguUyZ2ybshQb7LpWn8LBea1M-"
    "6sN7MkYf4W8WRSaB5AG5N4LNZQBlHFwHgI4LivtPVrcl3qM-jxTt_yWX5xSJmUJ-"
    "yLsjcDKNaG9X6jaS7mCn0crQQXiBI8toRWFjMOHF0CMWze28CebCq0Yp4mPtbVPtDhba9SoACDXJPaJCET0mrUiPVAQEWD"
    "qUNzlU23HKRzoMLCfIjKguF5_4cVRe4p1hsFxMuWeJtBWWAnO12D_L_"
    "8BsBSMNxfgX9GtUaP7MVYZOJy31mPujZYgjuu6H3tSAXKTDWGgjQkNIEHqLrPPni87JBGtv6_s3qmUofRQKdP2FXB-"
    "i5IRAp6um4FtIKYZJagFi3mR8T7B2WxwNITeXDVvLPSr-shFRVsaw328aoK8mcKfxzisroy7Qdf9Kk_"
    "VJzTn1v2LnDTh7gFhtMs9lHojCJEdneZOxWBrm83sM5CfsxytH4PnEAH2ocAxyO_WoLGt8b-4eRJtad5s_hif-"
    "FZch4DMRJ4YJZgJfayayuODe0NQ9mWPFD0Byzu9jbJb6cLPtgqDj6BAVgpqgQRVNKiTfrhnIecuQscpeSnjFkrtrhSjJfZ"
    "OR_bi2uOuHfq8ed8R_ZoUgqQ_bLccO7Wno0WDVSLp_"
    "v8su6tvrNJjkmhCrUyoZ5zYx9u8KNaEO6EB7vqTDAkAwOLHWD7ATw2kvuNAHu_"
    "n5iViBfoukmrqBFJeVE4gDOGwWyXrApdpl2gwSYV_"
    "BwLS00W5VXIrfAhgbOWpDU10LwQj4tyzrnRfJamyU9dFexH6tnyNtSodhYR_eMExXYMvwXOSEYyrOUL9DdFqGjWFqS_BD";
static const std::string TEST_ENCRYPTED_ACCESS_TOKEN_CHUNK_2 =
    "VXg6QjQ-XOdof0QYYCcZ6jAORvbXB_n9Rf4dQQ8sssMJX3i2_b2qxfiWIKJGyR-RzzmNsakzKXnafa4xUUpF87gSlM_"
    "OAaj1dZgvh49QCdmZJWfi49S6b0jFJE7yNfAEUzsFvN78ivOvYzjsLoJO1It1tVcfiTlkSNJ3039tXNNyiKRDECOKpinTf"
    "d1ZjWBjKF5FKxu5riPxwupgwjX9O-su3qW-puEl-oG04PwqrydYiQfR6cwUGN0jor0xJeW-L-"
    "7OfMdGdKP8CWAQH8ZeqRGJFjUsRH3GNNcfh4qPG3vEvyGYoN-"
    "yVRirjTKPjPNAARq5PoMaR742aEpKSYkomBFz4KsZhhjpLBSGgH0cQ22YPJ-loXlgI9_9wD4Y8csl6OYHQnpHi1bUSB0r-"
    "JZKNxQJAcoqoMGxxW2IULvJdPRTK6ooKpqnCg6C4V0ZmeczNLQ2VgHIzZ9gZd76nXyqmm1QmnWf7DDqlSgMq2cGMiD6BzW"
    "mBuxQC33uCI47QLZ036-tvyxCfxa_Cqd8GHvM5Vd8bb1ZKgkh9jvzcvi4bSyXGF6b0yu5_"
    "vRKp5GQdpUqLklyUR2Le1gDB8cSwBdxGyTZhD5dYK7qvClow0yAQhT2AwFSIMr16-nnq-DcUaRjOr4RN4GsKQ5SP-"
    "IebmtMhMWtJ7V5uCrIZLOPByXWr5q7CutXLKWejhomSZlrUwol1QnDuxDKBulcXtn9PLHGMJkSngizlc18a8zG3C_"
    "YiV15xI8XNgvY2bijXxf2HbYG81rSMZ_jbUO_u9URPwkfMbRRtQ5Llp4K-5m_tFods1pV3fBFK3tUe-ZLrD7JsjPYSSLx-"
    "2E82zdEhPagepsJz-xeTYItSeiMl1xff_zJ-NfrEBNec15Zr1kWfeL2Q-"
    "8Fgu1ne4psafSPFqJOQ3PiRsxdrLoIU0mb0v6lzdwFULUBbcKiwrZ2Pnt0ZrEo1bXgPnCUVo1KajgSZxZaT-"
    "uXE6qASF3VfC6BnMZOiSMvgYNYra_zVbWDhKz-lEovxbVrm19zKHVBR05dzq005GB_"
    "qvkeiFFAiNyQKEDsxAKcFMDsDpR0zOd5y2kFoyRKzfOK1LZjnTbyKB1BZ7KceIzsA-"
    "iZr9IsJysHrwnLZ4DGss34dJSRTzzq6MKz4W0fmDIyeNdijGeCkPnrU40cjxRqqsaYiGAXI-D101sqXB5D-"
    "i9ucQgPqkckjS00Ygz05h6g4JgaPb0ORBmpWQDev2RwOe8tWnR3pNa06sd8epuBc_"
    "jYhbF90z2SjwZartmhgfR2tjuA056YpASKtxp22hAi0kMvgmgKpXDdyoYRBbm6kjeUp8Qv57w6PGbhW9RkdK_"
    "PzFq3CNZiyrZIJIrknKQCWZ_wPCnYnfNlVjIL-9Zgz1yCxDRFvn24B0cAwECL1QN2SaqaFSubq_vsOc6g1u8tMR3NO_"
    "CdJcNAFTMzg3srQpBueScFasEwuDSaWXwQDQpLTVlb-I265nCGPrfEQb0jEMZqbJ7_"
    "NBD9yUPFJNRFYetZRY9ccXRCD32ZoZ5qV21K5LPr2AwGBTduGbhUJ7RKJM4QB3Jh784cQgXuQ56QtQZCeqgxfh9Z-"
    "o94sldBFNy4EDDetZMDH5wdrg5fzqV0XYvp7A1NNNGApumOjxPyoLPcdCtH4qR8qDV8qcPkj-"
    "UjLPUaZU4fn2m4ZCGcYth5Fzww4AdE9kiQSPZcYgBM_8SOlMEN-"
    "5Ca6AgYPUyaNXkeRKx8PTmghzRXkKIM8FuOYMIFEIygvcZTJcdcBwt0aozDMR4kKVygvHTH5leexCQ8gShI05o3POevPCp"
    "h6dflfj-"
    "oQTy16zGmAFLcjyEQUvSoE3ifqG1GqOIuuLPbb95PuwD0zs3VaETN1OQevROOkvIEeoXof1CGBvwwRrIh7Ym5eAig-"
    "RpM30wmjZYeiZGE_QQBbUgBsTVqE-E_Ioh2Ssm5TcBVMRzFRbtgnAKtUMlvcZKblAJARVH8bSkq3XYvGI1n4zetyGamN_"
    "uxRW7jSQqFQWFz_hQPLWlo9vaLR-YBAHl7ye9Pxqde5k4t_"
    "I6RPoZBPZMG49hMqFBQ7ksbq2b3uNtJ9fjD8vZx8pfRdzKfuvdpBBn83w7rSDjQoJi5BcXCf3zLCCD3-"
    "qxdaDRmcbe5sTN60HkoBAAy7-1na1AveKpaV9t0reBLXH78JyAowxLrcJKPWKnMy6SsI15z0YAEkIR_8TkRBAWOrQo1O_"
    "7GtfJ5QvnLo7Ph2MFmtTKV-R4uxjj_OdCdpnQTtwQw_3lxshx2atrscyOU-K-whXs7PrvsJj2onWtCFlS0pje7eK5ntlV_"
    "XaEOw_oMzwPG_"
    "eYaAnjyBOg9bRCfPhAQVHRYFnxEUkKyAOfd2F82t9OHxb0pCCPOhKoP5jcUJ664o5NhodeTj6wP1zXDFp-"
    "mIhycgKRAkS5Ss-kTCcuz-Kn2Kyr7b7XGhCUeb7RMBOUfAkxYmLzca584oupQ3CA56mBZNf0n-"
    "W9DvVHcHAUPbYRR5FUbfUG1QTRThVwVj50glNpcGjIQB0zWuiMKC78dayTg2VmkFgfM5xIIIWM33uXVan2KI6ZGCJXzIur"
    "OnbHHOiVzWEmLS86LvBggBmZGeKsnq9o9Su2eoOf_kJWLN9TbdVwnxFdCHs-OubdjBclBZpXxTZkIc";

namespace {
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);
}

class MockSecretReader : public SecretReader {
public:
  const std::string& clientSecret() const override {
    CONSTRUCT_ON_FIRST_USE(std::string, "asdf_client_secret_fdsa");
  }
  const std::string& hmacSecret() const override {
    CONSTRUCT_ON_FIRST_USE(std::string, TEST_HMAC_SECRET);
  }
};

class MockOAuth2CookieValidator : public CookieValidator {
public:
  MOCK_METHOD(std::string&, username, (), (const));
  MOCK_METHOD(std::string&, token, (), (const));
  MOCK_METHOD(std::string&, refreshToken, (), (const));

  MOCK_METHOD(bool, canUpdateTokenByRefreshToken, (), (const));
  MOCK_METHOD(bool, isValid, (), (const));
  MOCK_METHOD(void, setParams, (const Http::RequestHeaderMap& headers, const std::string& secret));
};

class MockOAuth2Client : public OAuth2Client {
public:
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {}
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {}
  void setCallbacks(FilterCallbacks&) override {}
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

  MOCK_METHOD(void, cancel, (), (override));

  MOCK_METHOD(void, asyncGetAccessToken,
              (const std::string&, const std::string&, const std::string&, const std::string&,
               const std::string&, Envoy::Extensions::HttpFilters::Oauth2::AuthType));

  MOCK_METHOD(void, asyncRefreshAccessToken,
              (const std::string&, const std::string&, const std::string&,
               Envoy::Extensions::HttpFilters::Oauth2::AuthType));
};

class OAuth2Test : public testing::Test {
public:
  OAuth2Test(bool run_init = true) : request_(&cm_.thread_local_cluster_.async_client_) {
    factory_context_.server_factory_context_.cluster_manager_.initializeClusters(
        {"auth.example.com"}, {});
    if (run_init) {
      init();
    }
  }

  void init() { init(getConfig()); }

  void init(FilterConfigSharedPtr config) {
    // Set up the OAuth client.
    oauth_client_ = new MockOAuth2Client();
    std::unique_ptr<OAuth2Client> oauth_client_ptr{oauth_client_};

    config_ = config;
    ON_CALL(test_random_, random()).WillByDefault(Return(123456789));
    filter_ = std::make_shared<OAuth2Filter>(config_, std::move(oauth_client_ptr), test_time_,
                                             test_random_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    validator_ = std::make_shared<MockOAuth2CookieValidator>();
    filter_->validator_ = validator_;
  }

  void initWithRealValidator(FilterConfigSharedPtr config) {
    oauth_client_ = new MockOAuth2Client();
    std::unique_ptr<OAuth2Client> oauth_client_ptr{oauth_client_};
    validator_.reset();
    config_ = config;
    ON_CALL(test_random_, random()).WillByDefault(Return(123456789));
    filter_ = std::make_shared<OAuth2Filter>(config_, std::move(oauth_client_ptr), test_time_,
                                             test_random_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  std::string cookieHeaderFromResponse(const Http::ResponseHeaderMap& headers) const {
    absl::flat_hash_map<std::string, std::string> cookies;
    headers.iterate([&cookies](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      if (header.key().getStringView() != Http::Headers::get().SetCookie.get()) {
        return Http::HeaderMap::Iterate::Continue;
      }
      const std::vector<absl::string_view> parts =
          absl::StrSplit(header.value().getStringView(), absl::MaxSplits(';', 1));
      const std::pair<absl::string_view, absl::string_view> name_value =
          absl::StrSplit(parts[0], absl::MaxSplits('=', 1));
      if (name_value.second == "deleted") {
        cookies.erase(name_value.first);
      } else {
        cookies.insert_or_assign(std::string(name_value.first), std::string(name_value.second));
      }
      return Http::HeaderMap::Iterate::Continue;
    });
    return absl::StrJoin(cookies, "; ", absl::PairFormatter("="));
  }

  const std::string& refreshTokenForTest() const { return filter_->refresh_token_; }

  // Set up proto fields with standard config.
  FilterConfigSharedPtr getConfig(
      bool forward_bearer_token = true, bool use_refresh_token = false,
      ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType auth_type =
          ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
              OAuth2Config_AuthType_URL_ENCODED_BODY,
      int default_refresh_token_expires_in = 0, bool preserve_authorization_header = false,
      bool disable_id_token_set_cookie = false, bool set_cookie_domain = false,
      bool disable_access_token_set_cookie = false, bool disable_refresh_token_set_cookie = false,
      ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite bearer_samesite =
          ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
              CookieConfig_SameSite_DISABLED,
      ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite hmac_samesite =
          ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
              CookieConfig_SameSite_DISABLED,
      ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite expires_samesite =
          ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
              CookieConfig_SameSite_DISABLED,
      ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite id_token_samesite =
          ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
              CookieConfig_SameSite_DISABLED,
      ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite refresh_token_samesite =
          ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
              CookieConfig_SameSite_DISABLED,
      ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite nonce_samesite =
          ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
              CookieConfig_SameSite_DISABLED,
      ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite code_verifier_samesite =
          ::envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
              CookieConfig_SameSite_DISABLED,
      int csrf_token_expires_in = 0, int code_verifier_token_expires_in = 0,
      bool disable_token_encryption = false) {

    envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
    auto* endpoint = p.mutable_token_endpoint();
    endpoint->set_cluster("auth.example.com");
    endpoint->set_uri("auth.example.com/_oauth");
    endpoint->mutable_timeout()->set_seconds(1);
    p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
    p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
    p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
    p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
    p.set_forward_bearer_token(forward_bearer_token);
    p.set_preserve_authorization_header(preserve_authorization_header);
    p.set_disable_id_token_set_cookie(disable_id_token_set_cookie);
    p.set_disable_access_token_set_cookie(disable_access_token_set_cookie);
    p.set_disable_refresh_token_set_cookie(disable_refresh_token_set_cookie);
    p.set_stat_prefix("my_prefix");

    auto* useRefreshToken = p.mutable_use_refresh_token();
    useRefreshToken->set_value(use_refresh_token);

    if (default_refresh_token_expires_in != 0) {
      auto* refresh_token_expires_in = p.mutable_default_refresh_token_expires_in();
      refresh_token_expires_in->set_seconds(default_refresh_token_expires_in);
    }

    if (csrf_token_expires_in != 0) {
      auto* expires_in = p.mutable_csrf_token_expires_in();
      expires_in->set_seconds(csrf_token_expires_in);
    }

    if (code_verifier_token_expires_in != 0) {
      auto* expires_in = p.mutable_code_verifier_token_expires_in();
      expires_in->set_seconds(code_verifier_token_expires_in);
    }

    p.set_auth_type(auth_type);
    p.add_auth_scopes("user");
    p.add_auth_scopes("openid");
    p.add_auth_scopes("email");
    p.add_resources("oauth2-resource");
    p.add_resources("http://example.com");
    p.add_resources("https://example.com/some/path%2F..%2F/utf8\xc3\x83;foo=bar?var1=1&var2=2");
    auto* matcher = p.add_pass_through_matcher();
    matcher->set_name(":method");
    matcher->mutable_string_match()->set_exact("OPTIONS");
    auto* deny_redirect_matcher = p.add_deny_redirect_matcher();
    deny_redirect_matcher->set_name("X-Requested-With");
    deny_redirect_matcher->mutable_string_match()->set_exact("XMLHttpRequest");
    auto credentials = p.mutable_credentials();
    credentials->set_client_id(TEST_CLIENT_ID);
    credentials->mutable_token_secret()->set_name("secret");
    credentials->mutable_hmac_secret()->set_name("hmac");
    // Skipping setting credentials.cookie_names field should give default cookie names:
    // BearerToken, OauthHMAC, and OauthExpires.
    if (set_cookie_domain) {
      credentials->set_cookie_domain("example.com");
    }

    // Initialize CookieConfigs
    auto* cookie_configs = p.mutable_cookie_configs();

    // Bearer Token Cookie Config
    auto* bearer_config = cookie_configs->mutable_bearer_token_cookie_config();
    bearer_config->set_same_site(bearer_samesite);

    // HMAC Cookie Config, Set value to disabled by default.
    auto* hmac_config = cookie_configs->mutable_oauth_hmac_cookie_config();
    hmac_config->set_same_site(hmac_samesite);

    // Set value to disabled by default.
    auto* expires_config = cookie_configs->mutable_oauth_expires_cookie_config();
    expires_config->set_same_site(expires_samesite);

    // Set value to disabled by default.
    auto* id_token_config = cookie_configs->mutable_id_token_cookie_config();
    id_token_config->set_same_site(id_token_samesite);

    // Set value to disabled by default.
    auto* refresh_token_config = cookie_configs->mutable_refresh_token_cookie_config();
    refresh_token_config->set_same_site(refresh_token_samesite);

    // Set value to disabled by default.
    auto* oauth_nonce_config = cookie_configs->mutable_oauth_nonce_cookie_config();
    oauth_nonce_config->set_same_site(nonce_samesite);

    // Set value to disabled by default.
    auto* code_verifier_config = cookie_configs->mutable_code_verifier_cookie_config();
    code_verifier_config->set_same_site(code_verifier_samesite);

    p.set_disable_token_encryption(disable_token_encryption);

    MessageUtil::validate(p, ProtobufMessage::getStrictValidationVisitor());

    // Create filter config.
    auto secret_reader = std::make_shared<MockSecretReader>();
    FilterConfigSharedPtr c = std::make_shared<FilterConfig>(
        p, factory_context_.server_factory_context_, secret_reader, scope_, "test.");

    return c;
  }

  // Test helpers exposing private OAuth2Filter methods. OAuth2Filter declares
  // `friend class OAuth2Test`, but `TEST_F(OAuth2Test, ...)` expands to a class
  // *derived* from OAuth2Test, and C++ friendship is not inherited — so the
  // test bodies cannot call the privates directly. These wrappers bridge that.
  //
  // Unlike the equivalent helpers on main, no `config_` priming is needed here:
  // on release/v1.37 OAuth2Filter::config_ is set in the constructor (there is
  // no per-route config-resolution path), so it is always non-null by
  // the time these helpers are invoked from a test body.
  std::string encryptTokenForTest(const std::string& token) const {
    return filter_->encryptToken(token);
  }
  std::string decryptTokenForTest(const std::string& ct) const { return filter_->decryptToken(ct); }
  static size_t maxEncryptedTokenSizeForTest(size_t token_size) {
    return OAuth2Filter::maxEncryptedTokenSize(token_size);
  }
  void setCookieExpiresForTest(const std::string& expires_in, const std::string& id_token_in,
                               const std::string& refresh_token_in) {
    filter_->expires_in_ = expires_in;
    filter_->expires_id_token_in_ = id_token_in;
    filter_->expires_refresh_token_in_ = refresh_token_in;
  }
  size_t cookieTailSizeForTest(int cookie_type) const {
    return filter_->cookieTailSize(cookie_type);
  }
  std::string buildCookieTailForTest(int cookie_type) const {
    return filter_->BuildCookieTail(cookie_type);
  }
  void decryptAndUpdateOAuthTokenCookiesForTest(Http::RequestHeaderMap& headers) {
    filter_->decryptAndUpdateOAuthTokenCookies(headers);
  }

  // Validates the behavior of the cookie validator.
  void expectValidCookies(const CookieNames& cookie_names, const std::string& cookie_domain) {
    // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
    test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

    const auto expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) + 10;

    Http::TestRequestHeaderMapImpl request_headers{
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Path.get(), "/anypath"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Cookie.get(),
         fmt::format("{}={}", cookie_names.oauth_expires_, expires_at_s)},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.bearer_token_, "=" + TEST_ENCRYPTED_ACCESS_TOKEN)},
        {Http::Headers::get().Cookie.get(),
         absl::StrCat(cookie_names.oauth_hmac_, "=oMh0+qk68Y4ya4JGQqT+Ja1Y1X58Sc8iATRxPPPG5Yc=")},
    };

    auto cookie_validator =
        std::make_shared<OAuth2CookieValidator>(test_time_, cookie_names, cookie_domain);
    EXPECT_EQ(cookie_validator->token(), "");
    EXPECT_EQ(cookie_validator->refreshToken(), "");
    cookie_validator->setParams(request_headers, TEST_HMAC_SECRET);

    EXPECT_TRUE(cookie_validator->hmacIsValid());
    EXPECT_TRUE(cookie_validator->timestampIsValid());
    EXPECT_TRUE(cookie_validator->isValid());
    EXPECT_FALSE(cookie_validator->canUpdateTokenByRefreshToken());

    // If we advance time beyond 10s the timestamp should no longer be valid.
    test_time_.advanceTimeWait(std::chrono::seconds(11));

    EXPECT_FALSE(cookie_validator->timestampIsValid());
    EXPECT_FALSE(cookie_validator->isValid());
  }

  NiceMock<Event::MockTimer>* attachmentTimeout_timer_{};
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::shared_ptr<MockOAuth2CookieValidator> validator_;
  std::shared_ptr<OAuth2Filter> filter_;
  MockOAuth2Client* oauth_client_;
  FilterConfigSharedPtr config_;
  Http::MockAsyncClientRequest request_;
  std::deque<Http::AsyncClient::Callbacks*> callbacks_;
  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  Event::SimulatedTimeSystem test_time_;
  NiceMock<Random::MockRandomGenerator> test_random_;
};

// Verifies that the OAuth SDSSecretReader correctly updates dynamic generic secret.
TEST_F(OAuth2Test, SdsDynamicGenericSecret) {
  NiceMock<Server::MockConfigTracker> config_tracker;
  Secret::SecretManagerImpl secret_manager{config_tracker};
  envoy::config::core::v3::ConfigSource config_source;

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Init::MockManager> init_manager;
  Init::TargetHandlePtr init_handle;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(secret_context.server_context_, localInfo()).WillRepeatedly(ReturnRef(local_info));
  EXPECT_CALL(secret_context.server_context_, api()).WillRepeatedly(ReturnRef(*api));
  EXPECT_CALL(secret_context.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_handle](const Init::Target& target) {
        init_handle = target.createHandle("test");
      }));

  auto client_secret_provider = secret_manager.findOrCreateGenericSecretProvider(
      config_source, "client", secret_context, init_manager);
  auto client_callback = secret_context.cluster_manager_.subscription_factory_.callbacks_;
  auto token_secret_provider = secret_manager.findOrCreateGenericSecretProvider(
      config_source, "token", secret_context, init_manager);
  auto token_callback = secret_context.cluster_manager_.subscription_factory_.callbacks_;

  NiceMock<ThreadLocal::MockInstance> tls;
  SDSSecretReader secret_reader(std::move(client_secret_provider), std::move(token_secret_provider),
                                tls, *api);
  EXPECT_TRUE(secret_reader.clientSecret().empty());
  EXPECT_TRUE(secret_reader.hmacSecret().empty());

  const std::string yaml_client = R"EOF(
name: client
generic_secret:
  secret:
    inline_string: "client_test"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(yaml_client, typed_secret);
  const auto decoded_resources_client = TestUtility::decodeResources({typed_secret});

  EXPECT_TRUE(client_callback->onConfigUpdate(decoded_resources_client.refvec_, "").ok());
  EXPECT_EQ(secret_reader.clientSecret(), "client_test");
  EXPECT_EQ(secret_reader.hmacSecret(), "");

  const std::string yaml_token = R"EOF(
name: token
generic_secret:
  secret:
    inline_string: "token_test"
)EOF";
  TestUtility::loadFromYaml(yaml_token, typed_secret);
  const auto decoded_resources_token = TestUtility::decodeResources({typed_secret});

  EXPECT_TRUE(token_callback->onConfigUpdate(decoded_resources_token.refvec_, "").ok());
  EXPECT_EQ(secret_reader.clientSecret(), "client_test");
  EXPECT_EQ(secret_reader.hmacSecret(), "token_test");

  const std::string yaml_client_recheck = R"EOF(
name: client
generic_secret:
  secret:
    inline_string: "client_test_recheck"
)EOF";
  TestUtility::loadFromYaml(yaml_client_recheck, typed_secret);
  const auto decoded_resources_client_recheck = TestUtility::decodeResources({typed_secret});

  EXPECT_TRUE(client_callback->onConfigUpdate(decoded_resources_client_recheck.refvec_, "").ok());
  EXPECT_EQ(secret_reader.clientSecret(), "client_test_recheck");
  EXPECT_EQ(secret_reader.hmacSecret(), "token_test");
}

// Verifies that we fail constructing the filter if the authorization endpoint isn't a valid URL.
TEST_F(OAuth2Test, InvalidAuthorizationEndpoint) {
  // Create a filter config with an invalid authorization_endpoint URL.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  p.set_authorization_endpoint("INVALID_URL");
  auto secret_reader = std::make_shared<MockSecretReader>();

  EXPECT_THROW_WITH_MESSAGE(
      std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_, secret_reader,
                                     scope_, "test."),
      EnvoyException, "OAuth2 filter: invalid authorization endpoint URL 'INVALID_URL' in config.");
}

// Verifies that the OAuth config is created with a default value for auth_scopes field when it is
// not set in proto/yaml.
TEST_F(OAuth2Test, DefaultAuthScope) {
  // Set up proto fields with no auth scope set.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  p.set_forward_bearer_token(true);
  auto* matcher = p.add_pass_through_matcher();
  matcher->set_name(":method");
  matcher->mutable_string_match()->set_exact("OPTIONS");

  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");

  MessageUtil::validate(p, ProtobufMessage::getStrictValidationVisitor());

  // Create the OAuth config.
  auto secret_reader = std::make_shared<MockSecretReader>();
  FilterConfigSharedPtr test_config_;
  test_config_ = std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_,
                                                secret_reader, scope_, "test.");

  // resource is optional
  EXPECT_EQ(test_config_->encodedResourceQueryParams(), "");

  // Recreate the filter with current config and test if the scope was added
  // as a query parameter in response headers.
  init(test_config_);
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},

      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_DEFAULT_SCOPE + "&state=" + TEST_ENCODED_STATE},
  };

  // explicitly tell the validator to fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(OAuth2Test, OnDestroyCancelsOAuthClient) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Options},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=some_oauth_hmac_value"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=some_oauth_expires_value"},
      {Http::Headers::get().Cookie.get(), "RefreshToken=some_refresh_token_value"},
      {Http::Headers::get().Cookie.get(), "OauthNonce.00000000075bcd15=some_oauth_nonce_value"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier.00000000075bcd15=some_code_verifier_value"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(*oauth_client_, cancel());
  filter_->onDestroy();
}

// Verifies that the CSRF token cookie expiration (Max-Age) uses the custom
// value from csrf_token_expires_in configuration.
TEST_F(OAuth2Test, CustomCsrfTokenExpiresIn) {
  // Create a filter config with a custom CSRF token expiration.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");

  // Set custom CSRF token expiration
  const int custom_csrf_token_expires_in = 1234;
  auto* csrf_token_expires_in = p.mutable_csrf_token_expires_in();
  csrf_token_expires_in->set_seconds(custom_csrf_token_expires_in);

  // Create the OAuth config.
  auto secret_reader = std::make_shared<MockSecretReader>();
  FilterConfigSharedPtr test_config_;
  test_config_ = std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_,
                                                secret_reader, scope_, "test.");

  init(test_config_);
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Explicitly tell the validator to fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));

  // Verify that the CSRF token cookie (OauthNonce) expiration is set to the custom value.
  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN +
           ";path=/;Max-Age=" + std::to_string(custom_csrf_token_expires_in) + ";secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_DEFAULT_SCOPE + "&state=" + TEST_ENCODED_STATE},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

// Verifies that the code verifier token cookie expiration (Max-Age) uses the custom
// value from code_verifier_token_expires_in configuration.
TEST_F(OAuth2Test, CustomCodeVerifierTokenExpiresIn) {
  // Create a filter config with a custom code verifier token expiration.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");

  // Set custom code verifier token expiration
  const int custom_code_verifier_token_expires_in = 1234;
  auto* code_verifier_token_expires_in = p.mutable_code_verifier_token_expires_in();
  code_verifier_token_expires_in->set_seconds(custom_code_verifier_token_expires_in);

  // Create the OAuth config.
  auto secret_reader = std::make_shared<MockSecretReader>();
  FilterConfigSharedPtr test_config_;
  test_config_ = std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_,
                                                secret_reader, scope_, "test.");

  init(test_config_);
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Explicitly tell the validator to fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));

  // Verify that the CSRF token cookie (OauthNonce) expiration is set to the custom value.
  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=" +
           std::to_string(custom_code_verifier_token_expires_in) + ";secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_DEFAULT_SCOPE + "&state=" + TEST_ENCODED_STATE},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

// Verifies that query parameters in the authorization_endpoint URL are preserved.
TEST_F(OAuth2Test, PreservesQueryParametersInAuthorizationEndpoint) {
  // Create a filter config with an authorization_endpoint URL with query parameters.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/?foo=bar");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");

  // Create the OAuth config.
  auto secret_reader = std::make_shared<MockSecretReader>();
  FilterConfigSharedPtr test_config_;
  test_config_ = std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_,
                                                secret_reader, scope_, "test.");
  init(test_config_);
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Explicitly tell the validator to fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));

  // Verify that the foo=bar query parameter is preserved in the redirect.
  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&foo=bar"
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_DEFAULT_SCOPE + "&state=" + TEST_ENCODED_STATE},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(OAuth2Test, PreservesQueryParametersInAuthorizationEndpointWithUrlEncoding) {
  // Create a filter config with an authorization_endpoint URL with query parameters.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/?foo=bar");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");

  // Create the OAuth config.
  auto secret_reader = std::make_shared<MockSecretReader>();
  FilterConfigSharedPtr test_config_;
  test_config_ = std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_,
                                                secret_reader, scope_, "test.");
  init(test_config_);
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Explicitly tell the validator to fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));

  // Verify that the foo=bar query parameter is preserved in the redirect.
  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&foo=bar"
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_DEFAULT_SCOPE + "&state=" + TEST_ENCODED_STATE},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a sign out request.
 *
 * Expected behavior: the filter should redirect to the server name with cleared OAuth cookies.
 */
TEST_F(OAuth2Test, RequestSignout) {
  std::string long_token(5000, 'x');

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("BearerToken_0=", long_token.substr(0, 3955))},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("BearerToken_1=", long_token.substr(3955, 5000))},
      {Http::Headers::get().Cookie.get(), "BearerToken_chunks=2"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken_0=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken_1=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken_chunks=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().Location.get(), "https://traffic.example.com/"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a sign out request when end session endpoint is configured.
 *
 * Expected behavior: the filter should redirect to the end session endpoint.
 */
TEST_F(OAuth2Test, RequestSignoutWhenEndSessionEndpointIsConfigured) {
  // Create a filter config with end session endpoint and openid scope.
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");
  p.set_end_session_endpoint("https://auth.example.com/oauth/logout");
  p.add_auth_scopes("openid");

  // Create the OAuth config.
  auto secret_reader = std::make_shared<MockSecretReader>();
  FilterConfigSharedPtr test_config_;
  test_config_ = std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_,
                                                secret_reader, scope_, "test.");
  init(test_config_);

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), "IdToken=xyztoken"},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().Location.get(), "https://auth.example.com/oauth/"
                                            "logout?id_token_hint=xyztoken&client_id=1&post_logout_"
                                            "redirect_uri=https%3A%2F%2Ftraffic.example.com%2F"},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a request to an arbitrary path with valid OAuth cookies
 * (cookie values and validation are mocked out)
 * In a real flow, the injected OAuth headers should be sanitized and replaced with legitimate
 * values.
 *
 * Expected behavior: the filter should let the request proceed, and sanitize the injected headers.
 */
TEST_F(OAuth2Test, OAuthOkPass) {
  Http::TestRequestHeaderMapImpl mock_request_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer injected_malice!"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer legit_token"},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  // Sanitized return reference mocking
  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(mock_request_headers, false));

  // Ensure that existing OAuth forwarded headers got sanitized.
  EXPECT_EQ(mock_request_headers, expected_headers);

  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_success").value(), 1);
}

/**
 * Scenario: The OAuth filter receives a request to an arbitrary path with valid OAuth cookies
 * (cookie values and validation are mocked out), but with an invalid token in the Authorization
 * header and forwarding bearer token is disabled.
 *
 * Expected behavior: the filter should sanitize the Authorization header and let the request
 * proceed.
 */
TEST_F(OAuth2Test, OAuthOkPassButInvalidToken) {
  init(getConfig(false /* forward_bearer_token */));

  Http::TestRequestHeaderMapImpl mock_request_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer injected_malice!"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  // Sanitized return reference mocking
  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(mock_request_headers, false));

  // Ensure that existing OAuth forwarded headers got sanitized.
  EXPECT_EQ(mock_request_headers, expected_headers);

  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_success").value(), 1);
}

/**
 * Scenario: The OAuth filter receives a request with a foreign token in the Authorization
 * header. This header should be forwarded when preserve authorization header is enabled
 * and forwarding bearer token is disabled.
 *
 * Expected behavior: the filter should forward the foreign token and let the request proceed.
 */
TEST_F(OAuth2Test, OAuthOkPreserveForeignAuthHeader) {
  init(getConfig(false /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
                 1200 /* default_refresh_token_expires_in */,
                 true /* preserve_authorization_header */));

  Http::TestRequestHeaderMapImpl mock_request_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer ValidAuthorizationHeader"},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer ValidAuthorizationHeader"},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  // Sanitized return reference mocking
  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(mock_request_headers, false));

  // Ensure that existing OAuth forwarded headers got sanitized.
  EXPECT_EQ(mock_request_headers, expected_headers);

  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_success").value(), 1);
}

TEST_F(OAuth2Test, SetBearerToken) {
  init(getConfig(false /* forward_bearer_token */, true /* use_refresh_token */));

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  // Sanitized return reference mocking
  // std::string legit_token{"legit_token"};
  // EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  TEST_CODE_VERIFIER, AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;"
                                             "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN + ";path=/;Max-Age=604800;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/original_path?var1=1&var2=2"},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));

  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_success").value(), 1);
}

TEST_F(OAuth2Test, SetBearerTokenWithEncryptionDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.oauth2_encrypt_tokens", "false"}});

  init(getConfig(false /* forward_bearer_token */, true /* use_refresh_token */));

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  TEST_CODE_VERIFIER, AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;"
                                             "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=access_code;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=some-id-token;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=some-refresh-token;path=/;Max-Age=604800;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/original_path?var1=1&var2=2"},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));

  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_success").value(), 1);
}

TEST_F(OAuth2Test, SetBearerTokenWithDisableTokenEncryptionConfig) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.oauth2_encrypt_tokens", "true"}});

  constexpr auto DisabledSameSite = ::envoy::extensions::filters::http::oauth2::v3::
      CookieConfig_SameSite::CookieConfig_SameSite_DISABLED;

  init(getConfig(false /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, false, false, false, DisabledSameSite, DisabledSameSite,
                 DisabledSameSite, DisabledSameSite, DisabledSameSite, DisabledSameSite,
                 DisabledSameSite, 0, 0, true /* disable_token_encryption */));

  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  TEST_CODE_VERIFIER, AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));

  Http::TestRequestHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;"
                                             "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=access_code;path=/;Max-Age=600;secure;HttpOnly"},
      // The IdToken and RefreshToken cookies should be unencrypted since token encryption is
      // disabled.
      {Http::Headers::get().SetCookie.get(),
       "IdToken=some-id-token;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=some-refresh-token;path=/;Max-Age=604800;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/original_path?var1=1&var2=2"},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));

  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_success").value(), 1);
}

/**
 * Scenario: The OAuth filter receives a request without valid OAuth cookies to a non-callback URL
 * (indicating that the user needs to re-validate cookies or get 401'd).
 * This also tests both a forwarded http protocol from upstream and a plaintext connection.
 *
 * Expected behavior: the filter should redirect the user to the OAuth server with the credentials
 * in the query parameters.
 */
TEST_F(OAuth2Test, OAuthErrorNonOAuthHttpCallback) {
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // This is the immediate response - a redirect to the auth cluster.
  Http::TestResponseHeaderMapImpl first_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES + "&state=" + TEST_ENCODED_STATE + "&resource=oauth2-resource" +
           "&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  // Check that the redirect includes the URL encoded query parameter characters
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

  // This represents the beginning of the OAuth filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(first_request_headers, false));

  // This represents the callback request from the authorization server.
  Http::TestRequestHeaderMapImpl second_request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Deliberately fail the HMAC validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  TEST_CODE_VERIFIER, AuthType::UrlEncodedBody));

  // Invoke the callback logic. As a side effect, state_ will be populated.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(second_request_headers, false));

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(config_->clusterName(), "auth.example.com");

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl second_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "fV62OgLipChTQQC3UFgDp+l5sCiSb3zt7nCoJiVivWw=;"
                                             "path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(), "OauthExpires=;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/original_path?var1=1&var2=2"},
  };

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

  filter_->finishGetAccessTokenFlow();

  // Deliberately fail the HMAC validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  TEST_CODE_VERIFIER, AuthType::UrlEncodedBody));

  // Invoke the callback logic. As a side effect, state_ will be populated.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(second_request_headers, false));

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(config_->clusterName(), "auth.example.com");
}

/**
 * Scenario: The OAuth filter receives a callback request with an error code
 */
TEST_F(OAuth2Test, OAuthErrorQueryString) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?error=someerrorcode"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "401"},
      {Http::Headers::get().ContentLength.get(), "18"}, // unauthorizedBodyMessage()
      {Http::Headers::get().ContentType.get(), "text/plain"},
  };

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_failure").value(), 1);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_success").value(), 0);
}

/**
 * Scenario: The OAuth filter receives a callback request from the OAuth server.
 *
 * Expected behavior: the filter should pause the request and call the OAuth client to get the
 * tokens.
 */
TEST_F(OAuth2Test, OAuthCallbackStartsAuthentication) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  TEST_CODE_VERIFIER, AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a callback request from the OAuth server that has
 * an invalid CodeVerifier cookie.
 *
 * Expected behavior: the filter should fail the request and return a 401 Unauthorized response.
 */
TEST_F(OAuth2Test, OAuthCallbackWithInvalidCodeVerifierCookie) {
  static const std::string invalid_encrypted_code_verifier = "Fc1bBwAAAAAVzVsHAAAAABjf";
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier=" + invalid_encrypted_code_verifier + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a callback request from the OAuth server that lacks
 * the CodeVerifier cookie.
 *
 * Expected behavior: the filter should fail the request and return a 401 Unauthorized response.
 */
TEST_F(OAuth2Test, OAuthCallbackWithoutCodeVerifierCookie) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a callback request from the OAuth server that lacks a CSRF
 * token. This scenario simulates a CSRF attack where the original OAuth request was inserted to the
 * user's browser by a malicious actor, and the user was tricked into clicking on the link.
 *
 * Expected behavior: the filter should fail the request and return a 401 Unauthorized response.
 */
TEST_F(OAuth2Test, OAuthCallbackStartsAuthenticationNoCsrfToken) {
  // {"url":"https://traffic.example.com/original_path?var1=1&var2=2"}
  static const std::string state_without_csrf_token =
      "eyJ1cmwiOiJodHRwczovL3RyYWZmaWMuZXhhbXBsZS5jb20vb3JpZ2luYWxfcGF0aD92YXIxPTEmdmFyMj0yIn0";
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + state_without_csrf_token},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a callback request from the OAuth server that has an invalid
 * CSRF token (without Dot). This scenario simulates a CSRF attack where the original OAuth request
 * was inserted to the user's browser by a malicious actor, and the user was tricked into clicking
 * on the link.
 *
 * Expected behavior: the filter should fail the request and return a 401 Unauthorized response.
 */
TEST_F(OAuth2Test, OAuthCallbackStartsAuthenticationInvalidCsrfTokenWithoutDot) {
  // {"url":"https://traffic.example.com/original_path?var1=1&var2=2","csrf_token":"${extracted}"}
  static const std::string state_with_invalid_csrf_token =
      "eyJ1cmwiOiJodHRwczovL3RyYWZmaWMuZXhhbXBsZS5jb20vb3JpZ2luYWxfcGF0aD92YXIxPTEmdmFyMj0yIiwiY3Ny"
      "Zl90b2tlbiI6IjAwMDAwMDAwMDc1YmNkMTUifQ";
  static const std::string invalid_csrf_token_cookie = "00000000075bcd15";
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + state_with_invalid_csrf_token},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + invalid_csrf_token_cookie + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a callback request from the OAuth server that has an invalid
 * CSRF token (hmac doesn't match). This scenario simulates a CSRF attack where the original OAuth
 * request was inserted to the user's browser by a malicious actor, and the user was tricked into
 * clicking on the link.
 *
 * Expected behavior: the filter should fail the request and return a 401 Unauthorized response.
 */
TEST_F(OAuth2Test, OAuthCallbackStartsAuthenticationInvalidCsrfTokenInvalidHmac) {
  // {"url":"https://traffic.example.com/original_path?var1=1&var2=2","csrf_token":"${extracted}"}
  static const std::string state_with_invalid_csrf_token =
      "eyJ1cmwiOiJodHRwczovL3RyYWZmaWMuZXhhbXBsZS5jb20vb3JpZ2luYWxfcGF0aD92YXIxPTEmdmFyMj0yIiwiY3Ny"
      "Zl90b2tlbiI6IjAwMDAwMDAwMDc1YmNkMTUuaW52YWxpZGhtYWMifQ";
  static const std::string invalid_csrf_token_cookie = "00000000075bcd15.invalidhmac";
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + state_with_invalid_csrf_token},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + invalid_csrf_token_cookie + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a callback request from the OAuth server that has a malformed
 * state. This scenario simulates a CSRF attack where the original OAuth request was inserted to the
 * user's browser by a malicious actor, and the user was tricked into clicking on the link.
 *
 * Expected behavior: the filter should fail the request and return a 401 Unauthorized response.
 */
TEST_F(OAuth2Test, OAuthCallbackStartsAuthenticationMalformedState) {
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));

  // {"url":"https://traffic.example.com/original_path?var1=1&var2=2","csrf_token":"}
  static const std::string state_with_invalid_csrf_token_json =
      "eyJ1cmwiOiJodHRwczovL3RyYWZmaWMuZXhhbXBsZS5jb20vb3JpZ2luYWxfcGF0aD92YXIxPTEmdmFyMj0yIiwiY3Ny"
      "Zl90b2tlbiI6In0";

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(),
       "/_oauth?code=123&state=" + state_with_invalid_csrf_token_json},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  // Deliberately fail the HMAC Validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: The OAuth filter receives a request with an invalid CSRF token cookie.
 * This scenario simulates an attacker trying to forge a CSRF token.
 *
 * Expected behavior: the filter will ignore the invalid CSRF token and generate a new one
 */
TEST_F(OAuth2Test, RedirectToOAuthServerWithInvalidCSRFToken) {
  static const std::string invalid_csrf_token = "00000000075bcd15.invalidhmac";
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), "OauthNonce=" + invalid_csrf_token},
  };

  // Explicitly fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES + "&state=" + TEST_ENCODED_STATE + "&resource=oauth2-resource" +
           "&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

/**
 * Scenario: Protoc in opted-in to allow OPTIONS requests to pass-through. This is important as
 * POST requests initiate an OPTIONS request first in order to ensure POST is supported. During a
 * preflight request where the client Javascript initiates a remote call to a different endpoint,
 * we don't want to fail the call immediately due to browser restrictions, and use existing
 * cookies instead (OPTIONS requests do not send OAuth cookies.)
 */
TEST_F(OAuth2Test, OAuthOptionsRequestAndContinue) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Options},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"}};

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Options},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(request_headers, expected_headers);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_passthrough").value(), 1);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_success").value(), 0);
}

/**
 * Scenario: The OAuth filter receives a request without valid OAuth cookies to a non-callback URL
 * that matches the deny_redirect_matcher.
 *
 * Expected behavior: the filter should should return 401 Unauthorized response.
 */
TEST_F(OAuth2Test, AjaxDoesNotRedirect) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
      {"X-Requested-With", "XMLHttpRequest"},
  };

  // explicitly tell the validator to fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  // Unauthorized response is expected instead of 302 redirect.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_EQ(1, config_->stats().oauth_failure_.value());
  EXPECT_EQ(0, config_->stats().oauth_unauthorized_rq_.value());
}

// Validates the behavior of the cookie validator.
TEST_F(OAuth2Test, CookieValidator) {
  expectValidCookies(CookieNames{"BearerToken", "OauthHMAC", "OauthExpires", "IdToken",
                                 "RefreshToken", "OauthNonce", "CodeVerifier"},
                     "");
}

// Validates the behavior of the cookie validator with custom cookie names.
TEST_F(OAuth2Test, CookieValidatorWithCustomNames) {
  expectValidCookies(CookieNames{"CustomBearerToken", "CustomOauthHMAC", "CustomOauthExpires",
                                 "CustomIdToken", "CustomRefreshToken", "CustomOauthNonce",
                                 "CustomCodeVerifier"},
                     "");
}

// Validates the behavior of the cookie validator with custom cookie domain.
TEST_F(OAuth2Test, CookieValidatorWithCookieDomain) {
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  auto cookie_names = CookieNames{"BearerToken",  "OauthHMAC",  "OauthExpires", "IdToken",
                                  "RefreshToken", "OauthNonce", "CodeVerifier"};
  const auto expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) + 5;

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(),
       fmt::format("{}={}", cookie_names.oauth_expires_, expires_at_s)},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat(cookie_names.bearer_token_, "=", TEST_ENCRYPTED_ACCESS_TOKEN)},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat(cookie_names.oauth_hmac_, "=PHLtlCLTIjfuAocmHmW8QzM3YSTRF6L+E3o6a1+TiS4=")},
  };

  auto cookie_validator =
      std::make_shared<OAuth2CookieValidator>(test_time_, cookie_names, "example.com");

  EXPECT_EQ(cookie_validator->token(), "");
  EXPECT_EQ(cookie_validator->refreshToken(), "");
  cookie_validator->setParams(request_headers, TEST_HMAC_SECRET);

  EXPECT_TRUE(cookie_validator->hmacIsValid());
  EXPECT_TRUE(cookie_validator->timestampIsValid());
  EXPECT_TRUE(cookie_validator->isValid());
}

// Validates the behavior of the cookie validator when the combination of some fields could be same.
TEST_F(OAuth2Test, CookieValidatorSame) {
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  auto cookie_names = CookieNames{"BearerToken",  "OauthHMAC",  "OauthExpires", "IdToken",
                                  "RefreshToken", "OauthNonce", "CodeVerifier"};
  const auto expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) + 5;

  // Host name is `traffic.example.com:101` and the expire time is 5.
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com:101"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(),
       fmt::format("{}={}", cookie_names.oauth_expires_, expires_at_s)},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat(cookie_names.bearer_token_, "=", TEST_ENCRYPTED_ACCESS_TOKEN)},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat(cookie_names.oauth_hmac_, "=eYef0itomg0CAjYygAfCLwmS2s1DaiL+N1Ql5V48o4o=")},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(test_time_, cookie_names, "");
  EXPECT_EQ(cookie_validator->token(), "");
  cookie_validator->setParams(request_headers, TEST_HMAC_SECRET);

  EXPECT_TRUE(cookie_validator->hmacIsValid());
  EXPECT_TRUE(cookie_validator->timestampIsValid());
  EXPECT_TRUE(cookie_validator->isValid());

  // If we advance time beyond 5s the timestamp should no longer be valid.
  test_time_.advanceTimeWait(std::chrono::seconds(6));

  EXPECT_FALSE(cookie_validator->timestampIsValid());
  EXPECT_FALSE(cookie_validator->isValid());

  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  const auto new_expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) + 15;

  // Host name is `traffic.example.com:10` and the expire time is 15.
  // HMAC should be different from the above one with the separator fix.
  Http::TestRequestHeaderMapImpl request_headers_second{
      {Http::Headers::get().Host.get(), "traffic.example.com:10"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(),
       fmt::format("{}={}", cookie_names.oauth_expires_, new_expires_at_s)},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat(cookie_names.bearer_token_, "=", TEST_ENCRYPTED_ACCESS_TOKEN)},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat(cookie_names.oauth_hmac_, "=VSTrKslW8ZNUqwgP+6Ocm1+7+NcF8GG/e1dqKsq14rc=")},
  };

  cookie_validator->setParams(request_headers_second, TEST_HMAC_SECRET);

  EXPECT_TRUE(cookie_validator->hmacIsValid());
  EXPECT_TRUE(cookie_validator->timestampIsValid());
  EXPECT_TRUE(cookie_validator->isValid());

  // If we advance time beyond 15s the timestamp should no longer be valid.
  test_time_.advanceTimeWait(std::chrono::seconds(16));

  EXPECT_FALSE(cookie_validator->timestampIsValid());
  EXPECT_FALSE(cookie_validator->isValid());
}

// Validates the behavior of the cookie validator when the expires_at value is not a valid integer.
TEST_F(OAuth2Test, CookieValidatorInvalidExpiresAt) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=notanumber"},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "OauthHMAC="
                                          "042KfjoL8OTsm8r4l6IO5dlxjzkaTDSyCaAibGI00bM="},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(
      test_time_,
      CookieNames{"BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken",
                  "OauthNonce", "CodeVerifier"},
      "");
  cookie_validator->setParams(request_headers, TEST_HMAC_SECRET);

  EXPECT_TRUE(cookie_validator->hmacIsValid());
  EXPECT_FALSE(cookie_validator->timestampIsValid());
  EXPECT_FALSE(cookie_validator->isValid());
}

// Validates the behavior of the cookie validator when the expires_at value is not a valid integer.
TEST_F(OAuth2Test, CookieValidatorCanUpdateToken) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=notanumber"},
      {Http::Headers::get().Cookie.get(), "BearerToken=xyztoken;RefreshToken=dsdtoken;"},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(
      test_time_,
      CookieNames("BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken",
                  "OauthNonce", "CodeVerifier"),
      "");
  cookie_validator->setParams(request_headers, "mock-secret");

  EXPECT_TRUE(cookie_validator->canUpdateTokenByRefreshToken());
}

// Verify that we 401 the request if the state query param doesn't contain a valid URL.
TEST_F(OAuth2Test, OAuthTestInvalidUrlInStateQueryParam) {
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));

  static const std::string state_with_invalid_url =
      "eyJ1cmwiOiJibGFoIiwiY3NyZl90b2tlbiI6IjAwMDAwMDAwMDc1YmNkMTUubmE2a3J1NHgxcEhnb2NTSWVVL21kdEhZ"
      "bjU4R2gxYnF3ZVM0WFhvaXFWZz0ifQ";
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&state=" + state_with_invalid_url},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123"},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ===="},
      {Http::Headers::get().Cookie.get(), "OauthNonce=" + TEST_CSRF_TOKEN},
  };

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "401"},
      {Http::Headers::get().ContentLength.get(), "18"},
      {Http::Headers::get().ContentType.get(), "text/plain"},
      // Invalid URL: we inject a few : in the middle of the URL.
  };

  // Succeed the HMAC validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"access_code"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

// Verify that we 401 the request if the state query param contains the callback URL.
TEST_F(OAuth2Test, OAuthTestCallbackUrlInStateQueryParam) {
  // {"url":"https://traffic.example.com/_oauth","csrf_token":"${extracted}"}
  static const std::string state_with_callback_url =
      "eyJ1cmwiOiJodHRwczovL3RyYWZmaWMuZXhhbXBsZS5jb20vX29hdXRoIiwiY3NyZl90b2tlbiI6IjAwMDAwMDAwMDc1"
      "YmNkMTUubmE2a3J1NHgxcEhnb2NTSWVVL21kdEhZbjU4R2gxYnF3ZVM0WFhvaXFWZz0ifSA";

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&state=" + state_with_callback_url},

      {Http::Headers::get().Cookie.get(), "OauthExpires=123"},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ===="},
      {Http::Headers::get().Cookie.get(), "OauthNonce=" + TEST_CSRF_TOKEN},
  };

  Http::TestRequestHeaderMapImpl expected_response_headers{
      {Http::Headers::get().Status.get(), "401"},
      {Http::Headers::get().ContentLength.get(), "18"},
      {Http::Headers::get().ContentType.get(), "text/plain"},
  };

  // Succeed the HMAC validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"access_code"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(OAuth2Test, OAuthTestUpdatePathAfterSuccess) {
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
           "&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Cookie.get(), "OauthExpires=123"},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMj"
       "RlNjMxZTJmNTZkYzRmZTM0ZQ===="},
      {Http::Headers::get().Cookie.get(), "OauthNonce=" + TEST_CSRF_TOKEN},
  };

  Http::TestRequestHeaderMapImpl expected_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/original_path?var1=1&var2=2"},
  };

  // Succeed the HMAC validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  std::string legit_token{"access_code"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_EQ(request_headers.getHostValue(), "traffic.example.com");
  EXPECT_EQ(request_headers.getMethodValue(), Http::Headers::get().MethodValues.Get);
  EXPECT_EQ(request_headers.getPathValue(),
            "/_oauth?code=abcdefxyz123&scope=" + TEST_ENCODED_AUTH_SCOPES +
                "&state=" + TEST_ENCODED_STATE);
  auto auth_header = request_headers.get(Http::CustomHeaders::get().Authorization);
  EXPECT_EQ(auth_header[0]->value().getStringView(), "Bearer access_code");

  auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies["OauthExpires"], "123");
  EXPECT_EQ(cookies["BearerToken"], "access_code");
  EXPECT_EQ(
      cookies["OauthHMAC"],
      "ZTRlMzU5N2Q4ZDIwZWE5ZTU5NTg3YTU3YTcxZTU0NDFkMzY1ZTc1NjMyODYyMjRlNjMxZTJmNTZkYzRmZTM0ZQ====");
  EXPECT_EQ(cookies["OauthNonce"], TEST_CSRF_TOKEN);
}

/**
 * Testing oauth state with cookie domain.
 *
 * Expected behavior: Cookie domain should be set to the domain in the config.
 */
TEST_F(OAuth2Test, OAuthTestFullFlowPostWithCookieDomain) {
  init(getConfig(true, true,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, true /* set_cookie_domain */));
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // This is the immediate response - a redirect to the auth cluster.
  Http::TestResponseHeaderMapImpl first_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";domain=example.com;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER +
           ";domain=example.com;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES + "&state=" + TEST_ENCODED_STATE + "&resource=oauth2-resource" +
           "&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));

  // Check that the redirect includes URL encoded query parameter characters.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

  // This represents the beginning of the OAuth filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(first_request_headers, false));

  // This represents the callback request from the authorization server.
  Http::TestRequestHeaderMapImpl second_request_headers{
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";domain=example.com;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER +
           ";domain=example.com;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };
  // Deliberately fail the HMAC validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  TEST_CODE_VERIFIER, AuthType::UrlEncodedBody));

  // Invoke the callback logic. As a side effect, state_ will be populated.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(second_request_headers, false));

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(config_->clusterName(), "auth.example.com");

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  const std::chrono::seconds expiredTime(10);
  filter_->updateTokens("access_code", "some-id-token", "some-refresh-token", expiredTime);

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl second_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=seD1HFQMr2pDwXgZKYQ1+D8R/p8tCa2fO8xTmfAgAUg=;"
       "domain=example.com;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=10;domain=example.com;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN +
           ";domain=example.com;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN +
           ";domain=example.com;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN +
           ";domain=example.com;path=/;Max-Age=604800;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/original_path?var1=1&var2=2"},
  };

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

  filter_->finishGetAccessTokenFlow();
}

/**
 * Testing oauth state with special characters that must be escaped in JSON.
 *
 * Expected behavior: the JSON string in the state query parameter should be correctly escaped and
 * the final redirect should equal the original request.
 */
TEST_F(OAuth2Test, OAuthTestFullFlowPostWithSpecialCharactersForJson) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  const std::string url_with_special_characters =
      R"(/original_path?query="value"&key=val\ue#frag<ment>{data}[info]|test\^space)";
  const std::string test_encoded_state_with_special_characters =
      "eyJ1cmwiOiJodHRwczovL3RyYWZmaWMuZXhhbXBsZS5jb20vb3JpZ2luYWxfcGF0aD9xdWVyeT1cInZhbHVlXCIma2V5"
      "PXZhbFxcdWUjZnJhZzxtZW50PntkYXRhfVtpbmZvXXx0ZXN0XFxec3BhY2UiLCJjc3JmX3Rva2VuIjoiMDAwMDAwMDAw"
      "NzViY2QxNS5uYTZrcnU0eDFwSGdvY1NJZVUvbWR0SFluNThHaDFicXdlUzRYWG9pcVZnPSJ9";
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), url_with_special_characters},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // This is the immediate response - a redirect to the auth cluster.
  Http::TestResponseHeaderMapImpl first_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES + "&state=" + test_encoded_state_with_special_characters +
           "&resource=oauth2-resource" +
           "&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%"
           "3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // Fail the validation to trigger the OAuth flow.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));

  // Check that the redirect includes URL encoded query parameter characters.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

  // This represents the beginning of the OAuth filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(first_request_headers, false));

  // This represents the callback request from the authorization server.
  Http::TestRequestHeaderMapImpl second_request_headers{
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Path.get(),
       "/_oauth?code=123&state=" + test_encoded_state_with_special_characters},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };
  // Deliberately fail the HMAC validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  TEST_CODE_VERIFIER, AuthType::UrlEncodedBody));

  // Invoke the callback logic. As a side effect, state_ will be populated.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(second_request_headers, false));

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(config_->clusterName(), "auth.example.com");

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  const std::chrono::seconds expiredTime(10);
  filter_->updateTokens("access_code", "some-id-token", "some-refresh-token", expiredTime);

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl second_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "UzbL/bzvWEP8oaoPDfQrD0zu6zC6m0yBOowKx1Mdr6o=;"
                                             "path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(), "OauthExpires=10;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN + ";path=/;Max-Age=604800;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com" + url_with_special_characters},
  };

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

  filter_->finishGetAccessTokenFlow();
}

class DisabledIdTokenTests : public OAuth2Test {
public:
  DisabledIdTokenTests() : OAuth2Test(false) {
    // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
    test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

    request_headers_ = {
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Path.get(), "/_oauth"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
    };

    // Note no IdToken cookie below.
    expected_headers_ = {
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().SetCookie.get(),
         "OauthHMAC=" + hmac_without_id_token_ + ";path=/;Max-Age=600;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
    };

    init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                   ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                       OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
                   600 /* default_refresh_token_expires_in */,
                   false /* preserve_authorization_header */,
                   true /* disable_id_token_set_cookie */));

    EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(hmac_without_id_token_));
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  }

  std::string hmac_without_id_token_{"kEbe8eYQkIkoHDQSzf1e38bSXNrgFCSEUWHZtEX6Q4c="};
  const std::string access_code_{"access_code"};
  const std::string id_token_{"some-id-token"};
  const std::string refresh_token_{"some-refresh-token"};
  const std::chrono::seconds expires_in_{600};
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl expected_headers_;
};

// When disable_id_token_set_cookie is `true`, then during the access token flow the filter should
// *not* set the IdToken cookie in the 302 response and should produce an HMAC that does not
// consider the id-token.
TEST_F(DisabledIdTokenTests, SetCookieIgnoresIdTokenWhenDisabledAccessToken) {
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  expected_headers_.addCopy(Http::Headers::get().Location.get(), "");
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers_), true));

  // An ID token is still received from the IdP, but not set in the response headers above.
  filter_->onGetAccessTokenSuccess(access_code_, id_token_, refresh_token_, expires_in_);
}

// When disable_id_token_set_cookie is `true`, then during the refresh token flow the filter should
// *not* set the IdToken request header that's forwarded, the response headers that are returned,
// and should produce an HMAC that does not consider the id-token.
TEST_F(DisabledIdTokenTests, SetCookieIgnoresIdTokenWhenDisabledRefreshToken) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.oauth2_cleanup_cookies", "false"}});
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // An ID token is still received from the IdP, but not set in the request headers that are
  // forwarded.
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->onRefreshAccessTokenSuccess(access_code_, id_token_, refresh_token_, expires_in_);
  auto cookies = Http::Utility::parseCookies(request_headers_);
  const auto cookie_names = config_->cookieNames();
  EXPECT_EQ(cookies[cookie_names.oauth_hmac_], hmac_without_id_token_);
  EXPECT_EQ(cookies[cookie_names.oauth_expires_],
            "1600"); // Uses default_refresh_token_expires_in since not a legitimate JWT.
  EXPECT_EQ(cookies[cookie_names.bearer_token_], "access_code");
  EXPECT_EQ(cookies[cookie_names.refresh_token_], "some-refresh-token");
  EXPECT_EQ(cookies.contains(cookie_names.id_token_), false);

  // And ensure when the response comes back, it has the same cookies in the `expected_headers_`.
  Http::TestResponseHeaderMapImpl response_headers = {{Http::Headers::get().Status.get(), "302"}};
  filter_->encodeHeaders(response_headers, false);
  EXPECT_THAT(response_headers, HeaderMapEqualRef(&expected_headers_));
}

class DisabledTokenTests : public OAuth2Test {
public:
  DisabledTokenTests() : OAuth2Test(false) {
    // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
    test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

    request_headers_ = {
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Path.get(), "/_oauth"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
    };

    // Note no Token cookies below.
    expected_headers_ = {
        {Http::Headers::get().Status.get(), "302"},
        {Http::Headers::get().SetCookie.get(),
         "OauthHMAC=" + hmac_without_tokens_ + ";path=/;Max-Age=600;secure;HttpOnly"},
        {Http::Headers::get().SetCookie.get(),
         "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
    };

    init(getConfig(
        true /* forward_bearer_token */, true /* use_refresh_token */,
        ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
            OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
        600 /* default_refresh_token_expires_in */, false /* preserve_authorization_header */,
        true /* disable_id_token_set_cookie */, false /* set_cookie_domain */,
        true /* disable_access_token_set_cookie */, true /* disable_refresh_token_set_cookie */));

    EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(hmac_without_tokens_));
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  }

  std::string hmac_without_tokens_{"Crs4S83olTGsGL7jbxBWw37gvuv0P2WbOvGTr/F6Z0o="};
  const std::string access_code_{"access_code"};
  const std::string id_token_{"some-id-token"};
  const std::string refresh_token_{"some-refresh-token"};
  const std::chrono::seconds expires_in_{600};
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl expected_headers_;
};

// When disable_id_token_set_cookie is `true`, then during the access token flow the filter should
// *not* set the IdToken cookie in the 302 response and should produce an HMAC that does not
// consider the id-token.
TEST_F(DisabledTokenTests, SetCookieIgnoresTokensWhenAllTokensAreDisabled1) {
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  expected_headers_.addCopy(Http::Headers::get().Location.get(), "");
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers_), true));

  // All Tokens are still received from the IdP, but not set in the response headers above.
  filter_->onGetAccessTokenSuccess(access_code_, id_token_, refresh_token_, expires_in_);
}

// When disable_id_token_set_cookie is `true`, then during the refresh token flow the filter should
// *not* set the IdToken request header that's forwarded, the response headers that are returned,
// and should produce an HMAC that does not consider the id-token.
TEST_F(DisabledTokenTests, SetCookieIgnoresTokensWhenAllTokensAreDisabled2) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.oauth2_cleanup_cookies", "false"}});

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // All tokens are still received from the IdP, but not set in the request headers that are
  // forwarded.
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->onRefreshAccessTokenSuccess(access_code_, id_token_, refresh_token_, expires_in_);
  auto cookies = Http::Utility::parseCookies(request_headers_);
  const auto cookie_names = config_->cookieNames();
  EXPECT_EQ(cookies[cookie_names.oauth_hmac_], hmac_without_tokens_);
  EXPECT_EQ(cookies[cookie_names.oauth_expires_],
            "1600"); // Uses default_refresh_token_expires_in since not a legitimate JWT.
  EXPECT_EQ(cookies.contains(cookie_names.bearer_token_), false);
  EXPECT_EQ(cookies.contains(cookie_names.refresh_token_), false);
  EXPECT_EQ(cookies.contains(cookie_names.id_token_), false);

  // And ensure when the response comes back, it has the same cookies in the `expected_headers_`.
  Http::TestResponseHeaderMapImpl response_headers = {{Http::Headers::get().Status.get(), "302"}};
  filter_->encodeHeaders(response_headers, false);
  EXPECT_THAT(response_headers, HeaderMapEqualRef(&expected_headers_));
}

/**
 * Testing oauth response after tokens are set.
 *
 * Expected behavior: cookies are set.
 */

std::string oauthHMAC;

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithTokens) {
  oauthHMAC = "fueOhiagmqQRQSxerTj/KZ065YXYk5SOiLtEvm9qlyA=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithTokensUseRefreshToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  oauthHMAC = "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN + ";path=/;Max-Age=604800;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithTokensUseRefreshTokenAndDefaultRefreshTokenExpiresIn) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
                 1200 /* default_refresh_token_expires_in */));
  TestScopedRuntime scoped_runtime;
  oauthHMAC = "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN + ";path=/;Max-Age=1200;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

/**
 * Scenario: The Oauth filter saves cookies with tokens after successful receipt of the tokens.
 *
 * Expected behavior: The lifetime of the refresh token cookie is taken from the exp claim of the
 * refresh token.
 */

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithTokensUseRefreshTokenAndRefreshTokenExpiresInFromJwt) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
                 1200 /* default_refresh_token_expires_in */));
  oauthHMAC = "CmrSZUsPEF1D4UgEnuz2d2s878YnAoOpxQCtE9LJ89M=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  const std::string refreshToken =
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJ1bmlxdWVfbmFtZSI6ImFsZXhjZWk4OCIsInN1YiI6ImFsZXhjZWk4OCIsImp0aSI6IjQ5ZTFjMzc1IiwiYXVkIjoi"
      "dGVzdCIsIm5iZiI6MTcwNzQxNDYzNSwiZXhwIjoyNTU0NDE2MDAwLCJpYXQiOjE3MDc0MTQ2MzYsImlzcyI6ImRvdG5l"
      "dC11c2VyLWp3dHMifQ.LaGOw6x0-m7r-WzxgCIdPnAfp0O1hy6mW4klq9Vs2XM";
  const std::string encrypted_refresh_token =
      "Fc1bBwAAAAAVzVsHAAAAANmnPnluIb9exn3WlbkgaDHNTVoZUE-1O8H_"
      "amXtsHZWG04QXuzJxsFxxe58HpCeWYx7QYi886mP3fCWDBrOJZ4DkwJjQXtvp9VdmKhCr1qCYQ9mSdv6GY50g-aOOr-"
      "x1wXNGCfnURYA48u2BulYuHqG2FzNAfbPo8uNO0IS3CUNE3C9gLcs4gHq9AjMwXVe3PLxV0ihrcXCUVp0ao9R2k2Ki1V"
      "LZpaH6ntay0IUJft2hjvq3lVvtCakEH0LYmzx9G0MGwaqiaeeFBNQyCY9iji5BOAfFezKnLKAvsYn2egVDHEFXCCSUW2"
      "3YEA57eGNDrs1PIZXRvLrjyJCiBE-0Iiq74MgHSG6usBK21wks8VOGyIy3qRkz-LcmgLX9ZB1lA";

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + encrypted_refresh_token + ";path=/;Max-Age=2554415000;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", refreshToken,
                                   std::chrono::seconds(600));
}

/**
 * Scenario: The Oauth filter doesn't save cookie with refresh token because the token is expired.
 *
 * Expected behavior: The age of the cookie with refresh token is equal to zero.
 */

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithTokensUseRefreshTokenAndExpiredRefreshToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
                 1200 /* default_refresh_token_expires_in */));
  TestScopedRuntime scoped_runtime;
  oauthHMAC = "73RuBwU3Kx/7RP4N1yy+8QnhARjA15QOoxdKD7zk1pI=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(2554515000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  const std::string refreshToken =
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJ1bmlxdWVfbmFtZSI6ImFsZXhjZWk4OCIsInN1YiI6ImFsZXhjZWk4OCIsImp0aSI6IjQ5ZTFjMzc1IiwiYXVkIjoi"
      "dGVzdCIsIm5iZiI6MTcwNzQxNDYzNSwiZXhwIjoyNTU0NDE2MDAwLCJpYXQiOjE3MDc0MTQ2MzYsImlzcyI6ImRvdG5l"
      "dC11c2VyLWp3dHMifQ.LaGOw6x0-m7r-WzxgCIdPnAfp0O1hy6mW4klq9Vs2XM";
  const std::string encrypted_refresh_token =
      "Fc1bBwAAAAAVzVsHAAAAANmnPnluIb9exn3WlbkgaDHNTVoZUE-1O8H_"
      "amXtsHZWG04QXuzJxsFxxe58HpCeWYx7QYi886mP3fCWDBrOJZ4DkwJjQXtvp9VdmKhCr1qCYQ9mSdv6GY50g-aOOr-"
      "x1wXNGCfnURYA48u2BulYuHqG2FzNAfbPo8uNO0IS3CUNE3C9gLcs4gHq9AjMwXVe3PLxV0ihrcXCUVp0ao9R2k2Ki1V"
      "LZpaH6ntay0IUJft2hjvq3lVvtCakEH0LYmzx9G0MGwaqiaeeFBNQyCY9iji5BOAfFezKnLKAvsYn2egVDHEFXCCSUW2"
      "3YEA57eGNDrs1PIZXRvLrjyJCiBE-0Iiq74MgHSG6usBK21wks8VOGyIy3qRkz-LcmgLX9ZB1lA";

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=2554515600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + encrypted_refresh_token + ";path=/;Max-Age=0;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", refreshToken,
                                   std::chrono::seconds(600));
}

/**
 * Scenario: The Oauth filter receives the refresh token without exp claim.
 *
 * Expected behavior: The age of the cookie with refresh token is equal to default value.
 */

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithTokensUseRefreshTokenAndNoExpClaimInRefreshToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
                 1200 /* default_refresh_token_expires_in */));
  TestScopedRuntime scoped_runtime;
  oauthHMAC = "euROdA+Ca4p/9JoMnX50fiqHormIWP/S+Fse+wD+V8I=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  const std::string refreshToken =
      "eyJhbGciOiJIUzI1NiJ9."
      "eyJSb2xlIjoiQWRtaW4iLCJJc3N1ZXIiOiJJc3N1ZXIiLCJVc2VybmFtZSI6IkphdmFJblVzZSIsImlhdCI6MTcwODA2"
      "NDcyOH0.92H-X2Oa4ECNmFLZBWBHP0BJyEHDprLkEIc2JBJYwkI";
  const std::string encrypted_refresh_token =
      "Fc1bBwAAAAAVzVsHAAAAANmnPnluIb9exn3WlbkgaDE7Qej3gaQyBPqvzoNiSVn8-sv2lmZF7nT3OVnBe7X-KK-"
      "jOOVaiHesGNEsPt5F0CmkMytmf-t0VMASmnC8FhgnCsRkf2XHL_"
      "z18YGJTvbHgc6QDdKUDwGuMTL048BdQYelXZ9nwtNchSkbZIa8yUf5wrZtEvFpOzE-brHaI3LOWmHaQ27h_"
      "lm5eH0qKwMy_jXZMXhxzO_-Rrz9XBlVwIMP";

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + encrypted_refresh_token + ";path=/;Max-Age=1200;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", refreshToken,
                                   std::chrono::seconds(600));
}

/**
 * Scenario: The Oauth filter saves cookies with tokens after successful receipt of the tokens.
 *
 * Expected behavior: The lifetime of the id token cookie is taken from the exp claim of the
 * id token.
 */

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithTokensIdTokenExpiresInFromJwt) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
                 1200 /* default_refresh_token_expires_in */));
  TestScopedRuntime scoped_runtime;
  oauthHMAC = "MqrMKGLbdIEogLWZPRffaVTXDGRRveG3gn9bZu5Gd4Q=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  const std::string id_token =
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJ1bmlxdWVfbmFtZSI6ImFsZXhjZWk4OCIsInN1YiI6ImFsZXhjZWk4OCIsImp0aSI6IjQ5ZTFjMzc1IiwiYXVkIjoi"
      "dGVzdCIsIm5iZiI6MTcwNzQxNDYzNSwiZXhwIjoyNTU0NDE2MDAwLCJpYXQiOjE3MDc0MTQ2MzYsImlzcyI6ImRvdG5l"
      "dC11c2VyLWp3dHMifQ.LaGOw6x0-m7r-WzxgCIdPnAfp0O1hy6mW4klq9Vs2XM";
  const std::string encrypted_id_token =
      "Fc1bBwAAAAAVzVsHAAAAANmnPnluIb9exn3WlbkgaDHNTVoZUE-1O8H_"
      "amXtsHZWG04QXuzJxsFxxe58HpCeWYx7QYi886mP3fCWDBrOJZ4DkwJjQXtvp9VdmKhCr1qCYQ9mSdv6GY50g-aOOr-"
      "x1wXNGCfnURYA48u2BulYuHqG2FzNAfbPo8uNO0IS3CUNE3C9gLcs4gHq9AjMwXVe3PLxV0ihrcXCUVp0ao9R2k2Ki1V"
      "LZpaH6ntay0IUJft2hjvq3lVvtCakEH0LYmzx9G0MGwaqiaeeFBNQyCY9iji5BOAfFezKnLKAvsYn2egVDHEFXCCSUW2"
      "3YEA57eGNDrs1PIZXRvLrjyJCiBE-0Iiq74MgHSG6usBK21wks8VOGyIy3qRkz-LcmgLX9ZB1lA";

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + encrypted_id_token + ";path=/;Max-Age=2554415000;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN + ";path=/;Max-Age=1200;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", id_token, "some-refresh-token",
                                   std::chrono::seconds(600));
}

/**
 * Scenario: The Oauth filter doesn't save cookie with id token because the token is expired.
 *
 * Expected behavior: The age of the cookie with the id token is equal to zero.
 */

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithTokensExpiredIdToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
                 1200 /* default_refresh_token_expires_in */));
  TestScopedRuntime scoped_runtime;
  oauthHMAC = "eQmiVNw3uAZixmzqtd75kD/0MeSJzS/ROl99NNfWoyU=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(2554515000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  const std::string id_token =
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJ1bmlxdWVfbmFtZSI6ImFsZXhjZWk4OCIsInN1YiI6ImFsZXhjZWk4OCIsImp0aSI6IjQ5ZTFjMzc1IiwiYXVkIjoi"
      "dGVzdCIsIm5iZiI6MTcwNzQxNDYzNSwiZXhwIjoyNTU0NDE2MDAwLCJpYXQiOjE3MDc0MTQ2MzYsImlzcyI6ImRvdG5l"
      "dC11c2VyLWp3dHMifQ.LaGOw6x0-m7r-WzxgCIdPnAfp0O1hy6mW4klq9Vs2XM";
  const std::string encrypted_id_token =
      "Fc1bBwAAAAAVzVsHAAAAANmnPnluIb9exn3WlbkgaDHNTVoZUE-1O8H_"
      "amXtsHZWG04QXuzJxsFxxe58HpCeWYx7QYi886mP3fCWDBrOJZ4DkwJjQXtvp9VdmKhCr1qCYQ9mSdv6GY50g-aOOr-"
      "x1wXNGCfnURYA48u2BulYuHqG2FzNAfbPo8uNO0IS3CUNE3C9gLcs4gHq9AjMwXVe3PLxV0ihrcXCUVp0ao9R2k2Ki1V"
      "LZpaH6ntay0IUJft2hjvq3lVvtCakEH0LYmzx9G0MGwaqiaeeFBNQyCY9iji5BOAfFezKnLKAvsYn2egVDHEFXCCSUW2"
      "3YEA57eGNDrs1PIZXRvLrjyJCiBE-0Iiq74MgHSG6usBK21wks8VOGyIy3qRkz-LcmgLX9ZB1lA";

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=2554515600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + encrypted_id_token + ";path=/;Max-Age=0;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN + ";path=/;Max-Age=1200;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", id_token, "some-refresh-token",
                                   std::chrono::seconds(600));
}

/**
 * Scenario: The Oauth filter receives the id token without exp claim.
 *           This should never happen as the id token is a JWT with required exp claim per OpenID
 * Connect 1.0 specification.
 *
 * Expected behavior: The age of the cookie with id token is equal to the access token expiry.
 */

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithTokensNoExpClaimInIdToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY /* encoded_body_type */,
                 1200 /* default_refresh_token_expires_in */));
  TestScopedRuntime scoped_runtime;
  oauthHMAC = "CU0eIzpTJSD/LFOVPaH7ypOQqqBvh4s6Tin3ip9rajk=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  const std::string id_token =
      "eyJhbGciOiJIUzI1NiJ9."
      "eyJSb2xlIjoiQWRtaW4iLCJJc3N1ZXIiOiJJc3N1ZXIiLCJVc2VybmFtZSI6IkphdmFJblVzZSIsImlhdCI6MTcwODA2"
      "NDcyOH0.92H-X2Oa4ECNmFLZBWBHP0BJyEHDprLkEIc2JBJYwkI";
  const std::string encrypted_id_token =
      "Fc1bBwAAAAAVzVsHAAAAANmnPnluIb9exn3WlbkgaDE7Qej3gaQyBPqvzoNiSVn8-sv2lmZF7nT3OVnBe7X-KK-"
      "jOOVaiHesGNEsPt5F0CmkMytmf-t0VMASmnC8FhgnCsRkf2XHL_"
      "z18YGJTvbHgc6QDdKUDwGuMTL048BdQYelXZ9nwtNchSkbZIa8yUf5wrZtEvFpOzE-brHaI3LOWmHaQ27h_"
      "lm5eH0qKwMy_jXZMXhxzO_-Rrz9XBlVwIMP";

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + encrypted_id_token + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN + ";path=/;Max-Age=1200;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", id_token, "some-refresh-token",
                                   std::chrono::seconds(600));
}

TEST_F(OAuth2Test, OAuthBearerTokenFlowFromHeader) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/test?role=bearer"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::CustomHeaders::get().Authorization.get(), "Bearer xyz-header-token"},
  };

  // Fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(OAuth2Test, OAuthBearerTokenFlowFromQueryParameters) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/test?role=bearer&token=xyz-queryparam-token"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Fail the validation.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(OAuth2Test, CookieValidatorInTransition) {
  Http::TestRequestHeaderMapImpl request_headers_base64only{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=1600"},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "IdToken=" + TEST_ENCRYPTED_ID_TOKEN},
      {Http::Headers::get().Cookie.get(), "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=eK7Kw2VqlnZJiz93KTnZqUar3ajNAe+ubmosGFkyL4I="},
  };

  auto cookie_validator = std::make_shared<OAuth2CookieValidator>(
      test_time_,
      CookieNames{"BearerToken", "OauthHMAC", "OauthExpires", "IdToken", "RefreshToken",
                  "OauthNonce", "CodeVerifier"},
      "");
  cookie_validator->setParams(request_headers_base64only, "mock-secret");
  EXPECT_TRUE(cookie_validator->hmacIsValid());

  Http::TestRequestHeaderMapImpl request_headers_hexbase64{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthExpires=1600"},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "IdToken=" + TEST_ENCRYPTED_ID_TOKEN},
      {Http::Headers::get().Cookie.get(), "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC="
       "NzhhZWNhYzM2NTZhOTY3NjQ5OGIzZjc3MjkzOWQ5YTk0NmFiZGRhOGNkMDFlZmFlNmU2YTJjMTg1OTMyMmY4Mg=="},
  };
  cookie_validator->setParams(request_headers_hexbase64, "mock-secret");

  EXPECT_TRUE(cookie_validator->hmacIsValid());
}

// - The filter receives the initial request
// - The filter redirects a user to the authorization endpoint
// - The filter receives the callback request from the authorization endpoint
// - The filter gets a bearer and refresh tokens from the authorization endpoint
// - The filter redirects a user to the user agent with actual authorization data
// - The filter receives an other request when a bearer token is expired
// - The filter tries to update a bearer token via the refresh token instead of redirect user to the
// authorization endpoint
// - The filter gets a new bearer and refresh tokens via the current refresh token
// - The filter continues to handler the request without redirection to the user agent
TEST_F(OAuth2Test, OAuthTestFullFlowWithUseRefreshToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // This is the immediate response - a redirect to the auth cluster.
  Http::TestResponseHeaderMapImpl first_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES + "&state=" + TEST_ENCODED_STATE + "&resource=oauth2-resource" +
           "&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // Fail the validation to trigger the OAuth flow.

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));

  // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

  // This represents the beginning of the OAuth filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(first_request_headers, false));

  // This represents the callback request from the authorization server.
  Http::TestRequestHeaderMapImpl second_request_headers{
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";domain=example.com;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Cookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER +
           ";domain=example.com;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Path.get(), "/_oauth?code=123&state=" + TEST_ENCODED_STATE},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // Deliberately fail the HMAC validation check.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_CALL(*oauth_client_, asyncGetAccessToken("123", TEST_CLIENT_ID, "asdf_client_secret_fdsa",
                                                  "https://traffic.example.com" + TEST_CALLBACK,
                                                  TEST_CODE_VERIFIER, AuthType::UrlEncodedBody));

  // Invoke the callback logic. As a side effect, state_ will be populated.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(second_request_headers, false));

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(config_->clusterName(), "auth.example.com");

  // Expected response after the callback & validation is complete - verifying we kept the
  // state and method of the original request, including the query string parameters.
  Http::TestRequestHeaderMapImpl second_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "fV62OgLipChTQQC3UFgDp+l5sCiSb3zt7nCoJiVivWw=;"
                                             "path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(), "OauthExpires=;path=/;Max-Age=;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://traffic.example.com/original_path?var1=1&var2=2"},
  };

  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&second_response_headers), true));

  filter_->finishGetAccessTokenFlow();

  // the third request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl third_request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(third_request_headers, false));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  filter_->finishRefreshAccessTokenFlow();
  EXPECT_EQ(1, config_->stats().oauth_refreshtoken_success_.value());
  EXPECT_EQ(2, config_->stats().oauth_success_.value());
}

TEST_F(OAuth2Test, OAuthTestRefreshAccessTokenSuccess) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  // Fail the validation to trigger the OAuth flow with trying to get the access token using by
  // refresh token.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(first_request_headers, false));

  Http::TestResponseHeaderMapImpl redirect_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES + "&state=" + TEST_ENCODED_STATE +
           "resource=oauth2-resource&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com"},
  };

  // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  filter_->onRefreshAccessTokenSuccess("", "", "", std::chrono::seconds(10));

  EXPECT_EQ(1, config_->stats().oauth_refreshtoken_success_.value());
  EXPECT_EQ(1, config_->stats().oauth_success_.value());
}

TEST_F(OAuth2Test, OAuthTestRefreshAccessTokenFail) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  // Fail the validation to trigger the OAuth flow with trying to get the access token using by
  // refresh token.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(first_request_headers, false));

  Http::TestResponseHeaderMapImpl redirect_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES + "&state=" + TEST_ENCODED_STATE + "&resource=oauth2-resource" +
           "&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  // Check that the redirect includes the escaped parameter characters, '?', '&' and '='.
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&redirect_response_headers), true));

  filter_->onRefreshAccessTokenFailure();

  EXPECT_EQ(1, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(1, config_->stats().oauth_refreshtoken_failure_.value());
}

/**
 * Scenario: The OAuth filter refresh flow fails for a request that matches the
 * deny_redirect_matcher.
 *
 * Expected behavior: the filter should should return 401 Unauthorized response.
 */
TEST_F(OAuth2Test, AjaxRefreshDoesNotRedirect) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
      {"X-Requested-With", "XMLHttpRequest"},
  };

  std::string legit_token{"legit_token"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  // Fail the validation to trigger the OAuth flow with trying to get the access token using by
  // refresh token.
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(first_request_headers, false));

  // Unauthorized response is expected instead of 302 redirect.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Unauthorized, _, _, _, _));

  filter_->onRefreshAccessTokenFailure();

  EXPECT_EQ(0, config_->stats().oauth_unauthorized_rq_.value());
  EXPECT_EQ(1, config_->stats().oauth_refreshtoken_failure_.value());
  EXPECT_EQ(1, config_->stats().oauth_failure_.value());
}

TEST_F(OAuth2Test, OAuthTestSetCookiesAfterRefreshAccessToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));

  const auto expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) - 10;

  // the third request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), fmt::format("OauthExpires={}", expires_at_s)},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=dCu0otMcLoaGF73jrT+R8rGA0pnWyMgNf4+GivGrHEI="},
  };

  std::string legit_refresh_token{"legit_refresh_token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  const std::chrono::seconds expiredTime(10);
  filter_->updateTokens("access_code", "some-id-token", "some-refresh-token", expiredTime);

  filter_->finishRefreshAccessTokenFlow();

  Http::TestResponseHeaderMapImpl response_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Http::TestResponseHeaderMapImpl expected_response_headers{
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "UzbL/bzvWEP8oaoPDfQrD0zu6zC6m0yBOowKx1Mdr6o=;"
                                             "path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(), "OauthExpires=10;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN + ";path=/;Max-Age=604800;secure;HttpOnly"},
  };

  EXPECT_THAT(response_headers, HeaderMapEqualRef(&expected_response_headers));

  auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("BearerToken"), "access_code");
  EXPECT_EQ(cookies.at("IdToken"), "some-id-token");

  // OAuth flow cookies should be removed before forwarding the request
  EXPECT_EQ(cookies.contains("OauthHMAC"), false);
  EXPECT_EQ(cookies.contains("OauthExpires"), false);
  EXPECT_EQ(cookies.contains("RefreshToken"), false);
  EXPECT_EQ(cookies.contains("OauthNonce"), false);
  EXPECT_EQ(cookies.contains("CodeVerifier"), false);
}

// When a refresh flow succeeds, but a new refresh token isn't received from the OAuth server, the
// previously received refresh token should be set in the response cookies.
TEST_F(OAuth2Test, OAuthTestSetCookiesAfterRefreshAccessTokenNoNewRefreshToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));

  const auto expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) - 10;

  std::string legit_refresh_token = "legit_refresh_token";
  std::string encrypted_refresh_token =
      "Fc1bBwAAAAAVzVsHAAAAAOh8bHz59OyZPtKMgiX5FWJMyTXqsPjbf1j-Ao8fn1tb";
  // the third request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), fmt::format("OauthExpires={}", expires_at_s)},
      {Http::Headers::get().Cookie.get(), fmt::format("RefreshToken={}", encrypted_refresh_token)},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=dCu0otMcLoaGF73jrT+R8rGA0pnWyMgNf4+GivGrHEI="},
  };

  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::UrlEncodedBody));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  const std::chrono::seconds expiredTime(10);
  filter_->updateTokens("access_code", "some-id-token", "", expiredTime);

  filter_->finishRefreshAccessTokenFlow();

  Http::TestResponseHeaderMapImpl response_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Http::TestResponseHeaderMapImpl expected_response_headers{
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "xQCNvPMLwq3rF1dB/mSwyVz7kcIZai8pD8rS5SNLgRU=;"
                                             "path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(), "OauthExpires=10;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN + ";path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       fmt::format("RefreshToken={};path=/;Max-Age=604800;secure;HttpOnly",
                   encrypted_refresh_token)},
  };

  EXPECT_THAT(response_headers, HeaderMapEqualRef(&expected_response_headers));

  auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("BearerToken"), "access_code");
  EXPECT_EQ(cookies.at("IdToken"), "some-id-token");

  // OAuth flow cookies should be removed before forwarding the request
  EXPECT_EQ(cookies.contains("OauthHMAC"), false);
  EXPECT_EQ(cookies.contains("OauthExpires"), false);
  EXPECT_EQ(cookies.contains("RefreshToken"), false);
  EXPECT_EQ(cookies.contains("OauthNonce"), false);
  EXPECT_EQ(cookies.contains("CodeVerifier"), false);
}

TEST_F(OAuth2Test, OAuthTestSetCookiesAfterRefreshAccessTokenWithBasicAuth) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_BASIC_AUTH
                 /* authType */));

  // 1. Test sending a request with expired tokens.
  // Set the expiration time to 10 seconds in the past to simulate token expiration.
  const auto expires_at_s = DateUtil::nowToSeconds(test_time_.timeSystem()) - 10;

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), fmt::format("OauthExpires={}", expires_at_s)},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=dCu0otMcLoaGF73jrT+R8rGA0pnWyMgNf4+GivGrHEI="},
      {Http::Headers::get().Cookie.get(), "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN},
  };

  std::string legit_refresh_token{"some-refresh-token"};
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(legit_refresh_token));

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));

  // Filter should refresh the tokens using the refresh token because the tokens are expired and a
  // refresh token is available.
  EXPECT_CALL(*oauth_client_,
              asyncRefreshAccessToken(legit_refresh_token, TEST_CLIENT_ID,
                                      "asdf_client_secret_fdsa", AuthType::BasicAuth));

  // Filter should stop iteration because the tokens are expired.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  // 2. Test refresh flow succeeds.
  // The new tokens received from the refresh flow.
  const std::string access_token = "accessToken";
  const std::string id_token = "idToken";
  const std::string refresh_token = "refreshToken";
  const std::string encrypted_id_token = "Fc1bBwAAAAAVzVsHAAAAAPD4z8oLeVyvkfTcl_cw198";
  const std::string encrypted_access_token = "Fc1bBwAAAAAVzVsHAAAAAGUINzc06x19yQYjN4Kb-YA";
  const std::string encrypted_refresh_token = "Fc1bBwAAAAAVzVsHAAAAACWUO4LpH2VJBN_6jSUWDPg";

  // Filter should continue decoding because the tokens are refreshed.
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));
  const std::chrono::seconds expiredTime(10);
  filter_->updateTokens(access_token, id_token, refresh_token, expiredTime);

  filter_->finishRefreshAccessTokenFlow();

  Http::TestResponseHeaderMapImpl response_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Http::TestResponseHeaderMapImpl expected_response_headers{
      {Http::Headers::get().SetCookie.get(), "OauthHMAC="
                                             "OYnODPsSGabEpZ2LAiPxyjAFgN/7/5Xg24G7jUoUbyI=;"
                                             "path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(), "OauthExpires=10;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + encrypted_access_token + ";path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + encrypted_id_token + ";path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + encrypted_refresh_token + ";path=/;Max-Age=604800;secure;HttpOnly"},
  };

  // Test the response headers are set correctly with the new tokens.
  EXPECT_THAT(response_headers, HeaderMapEqualRef(&expected_response_headers));

  // Test the request headers are updated with the new tokens.
  auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("BearerToken"), "accessToken");
  EXPECT_EQ(cookies.at("IdToken"), "idToken");

  // OAuth flow cookies should be removed before forwarding the request
  EXPECT_EQ(cookies.contains("OauthHMAC"), false);
  EXPECT_EQ(cookies.contains("OauthExpires"), false);
  EXPECT_EQ(cookies.contains("RefreshToken"), false);
  EXPECT_EQ(cookies.contains("OauthNonce"), false);
  EXPECT_EQ(cookies.contains("CodeVerifier"), false);
}

// Test all cookies with STRICT SameSite
TEST_F(OAuth2Test, AllCookiesStrictSameSite) {
  using SameSite = envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite;
  init(getConfig(true, true,
                 envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, false, false, false, SameSite::CookieConfig_SameSite_STRICT,
                 SameSite::CookieConfig_SameSite_STRICT, SameSite::CookieConfig_SameSite_STRICT,
                 SameSite::CookieConfig_SameSite_STRICT, SameSite::CookieConfig_SameSite_STRICT,
                 SameSite::CookieConfig_SameSite_STRICT, SameSite::CookieConfig_SameSite_STRICT));
  oauthHMAC = "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;";
  TestScopedRuntime scoped_runtime;
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN +
           ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN +
           ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN +
           ";path=/;Max-Age=604800;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().Location.get(), ""},
  };

  filter_->decodeHeaders(request_headers, false);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

// Test all cookies with NONE SameSite
TEST_F(OAuth2Test, AllCookiesNoneSameSite) {
  using SameSite = envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite;
  init(getConfig(true, true,
                 envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, false, false, false, SameSite::CookieConfig_SameSite_NONE,
                 SameSite::CookieConfig_SameSite_NONE, SameSite::CookieConfig_SameSite_NONE,
                 SameSite::CookieConfig_SameSite_NONE, SameSite::CookieConfig_SameSite_NONE,
                 SameSite::CookieConfig_SameSite_NONE));
  oauthHMAC = "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;";
  TestScopedRuntime scoped_runtime;
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly;SameSite=None"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly;SameSite=None"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN +
           ";path=/;Max-Age=600;secure;HttpOnly;SameSite=None"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly;SameSite=None"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN +
           ";path=/;Max-Age=604800;secure;HttpOnly;SameSite=None"},
      {Http::Headers::get().Location.get(), ""},
  };

  filter_->decodeHeaders(request_headers, false);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

// Test all cookies with LAX SameSite
TEST_F(OAuth2Test, AllCookiesLaxSameSite) {
  using SameSite = envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite;
  init(getConfig(true, true,
                 envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, false, false, false, SameSite::CookieConfig_SameSite_LAX,
                 SameSite::CookieConfig_SameSite_LAX, SameSite::CookieConfig_SameSite_LAX,
                 SameSite::CookieConfig_SameSite_LAX, SameSite::CookieConfig_SameSite_LAX,
                 SameSite::CookieConfig_SameSite_LAX, SameSite::CookieConfig_SameSite_LAX));
  oauthHMAC = "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;";
  TestScopedRuntime scoped_runtime;
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN +
           ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN +
           ";path=/;Max-Age=604800;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().Location.get(), ""},
  };

  filter_->decodeHeaders(request_headers, false);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

// Test mixed SameSite configurations with some disabled
TEST_F(OAuth2Test, MixedCookieSameSiteWithDisabled) {
  using SameSite = envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite;
  init(getConfig(true, true,
                 envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, false, false, false, SameSite::CookieConfig_SameSite_STRICT,
                 SameSite::CookieConfig_SameSite_LAX, SameSite::CookieConfig_SameSite_DISABLED,
                 SameSite::CookieConfig_SameSite_NONE, SameSite::CookieConfig_SameSite_STRICT,
                 SameSite::CookieConfig_SameSite_DISABLED, SameSite::CookieConfig_SameSite_LAX));
  oauthHMAC = "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;";
  TestScopedRuntime scoped_runtime;
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN +
           ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly;SameSite=None"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN +
           ";path=/;Max-Age=604800;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().Location.get(), ""},
  };

  filter_->decodeHeaders(request_headers, false);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

// Test mixed SameSite configurations without disabled
TEST_F(OAuth2Test, MixedCookieSameSiteWithoutDisabled) {
  using SameSite = envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite;
  init(getConfig(true, true,
                 envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, false, false, false, SameSite::CookieConfig_SameSite_STRICT,
                 SameSite::CookieConfig_SameSite_LAX, SameSite::CookieConfig_SameSite_NONE,
                 SameSite::CookieConfig_SameSite_STRICT, SameSite::CookieConfig_SameSite_LAX,
                 SameSite::CookieConfig_SameSite_NONE, SameSite::CookieConfig_SameSite_LAX));
  oauthHMAC = "4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;";
  TestScopedRuntime scoped_runtime;
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };

  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly;SameSite=None"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN +
           ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN +
           ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN +
           ";path=/;Max-Age=604800;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().Location.get(), ""},
  };

  filter_->decodeHeaders(request_headers, false);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  filter_->onGetAccessTokenSuccess("access_code", "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

TEST_F(OAuth2Test, CSRFSameSiteWithCookieDomain) {
  using SameSite = envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite;
  init(getConfig(true, true,
                 envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, true, false, false, SameSite::CookieConfig_SameSite_DISABLED,
                 SameSite::CookieConfig_SameSite_DISABLED, SameSite::CookieConfig_SameSite_DISABLED,
                 SameSite::CookieConfig_SameSite_DISABLED, SameSite::CookieConfig_SameSite_DISABLED,
                 SameSite::CookieConfig_SameSite_STRICT, SameSite::CookieConfig_SameSite_LAX));
  // First construct the initial request to the oauth filter with URI parameters.
  Http::TestRequestHeaderMapImpl first_request_headers{
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Post},
      {Http::Headers::get().Scheme.get(), "https"},
  };

  // This is the immediate response - a redirect to the auth cluster.
  Http::TestResponseHeaderMapImpl first_response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN +
           ";domain=example.com;path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().SetCookie.get(),
       "CodeVerifier=" + TEST_ENCRYPTED_CODE_VERIFIER +
           ";domain=example.com;path=/;Max-Age=600;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().Location.get(),
       "https://auth.example.com/oauth/"
       "authorize/?client_id=" +
           TEST_CLIENT_ID + "&code_challenge=" + TEST_CODE_CHALLENGE +
           "&code_challenge_method=S256" +
           "&redirect_uri=https%3A%2F%2Ftraffic.example.com%2F_oauth"
           "&response_type=code"
           "&scope=" +
           TEST_ENCODED_AUTH_SCOPES + "&state=" + TEST_ENCODED_STATE + "&resource=oauth2-resource" +
           "&resource=http%3A%2F%2Fexample.com"
           "&resource=https%3A%2F%2Fexample.com%2Fsome%2Fpath%252F..%252F%2Futf8%C3%83%3Bfoo%3Dbar%"
           "3Fvar1%3D1%26var2%3D2"},
  };

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(false));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&first_response_headers), true));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(first_request_headers, false));
}

// Ensure that the token cookies are deleted when the tokens are cleared
TEST_F(OAuth2Test, CookiesDeletedWhenTokensCleared) {
  // Initialize with use_refresh_token set to false
  init(getConfig(true /* forward_bearer_token */, false /* use_refresh_token */));

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(),
       "OauthHMAC=4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ=;path=/"
       ";Max-Age=600;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().Cookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly;SameSite=None"},
      {Http::Headers::get().Cookie.get(),
       "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN +
           ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().Cookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN +
           ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
      {Http::Headers::get().Cookie.get(),
       "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN +
           ";path=/;Max-Age=604800;secure;HttpOnly;SameSite=Lax"},
      {Http::Headers::get().Cookie.get(),
       "OauthNonce=" + TEST_CSRF_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly;SameSite=Strict"},
  };

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  // Expect to clear the headers
  Http::TestResponseHeaderMapImpl response_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=8p68j+W60Z7OJUXYNYpVQfkb+XRUm01bM0M/xzTRVBU=;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(), "OauthExpires=10;path=/;Max-Age=10;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().SetCookie.get(),
       "RefreshToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"},
      {Http::Headers::get().Location.get(), ""},
  };
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  const std::chrono::seconds expiredTime(10);
  filter_->onGetAccessTokenSuccess("", "", "", expiredTime);
}

// Ensure that the token cookies are decrypted before forwarding the request
TEST_F(OAuth2Test, CookiesDecryptedBeforeForwarding) {
  // Initialize with use_refresh_token set to false
  init(getConfig(true /* forward_bearer_token */));

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ="},
      {Http::Headers::get().Cookie.get(), "OauthExpires=1600"},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "IdToken=" + TEST_ENCRYPTED_ID_TOKEN},
      {Http::Headers::get().Cookie.get(), "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN},
      {Http::Headers::get().Cookie.get(), "OauthNonce=" + TEST_CSRF_TOKEN},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  // return reference mocking
  std::string access_token{"access_code"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(access_token));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  // Expect the request headers to be updated with the decrypted tokens
  auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("BearerToken"), "access_code");
  EXPECT_EQ(cookies.at("IdToken"), "some-id-token");

  // OAuth flow cookies should be removed before forwarding the request
  EXPECT_EQ(cookies.contains("OauthHMAC"), false);
  EXPECT_EQ(cookies.contains("OauthExpires"), false);
  EXPECT_EQ(cookies.contains("RefreshToken"), false);
  EXPECT_EQ(cookies.contains("OauthNonce"), false);
  EXPECT_EQ(cookies.contains("CodeVerifier"), false);
}

// Ensure that the token cookies are decrypted before forwarding the request
TEST_F(OAuth2Test, CookiesDecryptedBeforeForwardingWithEncryptionDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.oauth2_encrypt_tokens", "false"}});

  // Initialize with use_refresh_token set to false
  init(getConfig(true /* forward_bearer_token */));

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ="},
      {Http::Headers::get().Cookie.get(), "OauthExpires=1600"},
      {Http::Headers::get().Cookie.get(), "BearerToken=access_code"},
      {Http::Headers::get().Cookie.get(), "IdToken=some-id-token"},
      {Http::Headers::get().Cookie.get(), "RefreshToken=some-refresh-token"},
      {Http::Headers::get().Cookie.get(), "OauthNonce=" + TEST_CSRF_TOKEN},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  // return reference mocking
  std::string access_token{"access_code"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(access_token));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  // Expect the request headers to be updated with the decrypted tokens
  auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("BearerToken"), "access_code");
  EXPECT_EQ(cookies.at("IdToken"), "some-id-token");

  // OAuth flow cookies should be removed before forwarding the request
  EXPECT_EQ(cookies.contains("OauthHMAC"), false);
  EXPECT_EQ(cookies.contains("OauthExpires"), false);
  EXPECT_EQ(cookies.contains("RefreshToken"), false);
  EXPECT_EQ(cookies.contains("OauthNonce"), false);
  EXPECT_EQ(cookies.contains("CodeVerifier"), false);
}

// Ensure that the token cookies are decrypted before forwarding the request
TEST_F(OAuth2Test, CookiesDecryptedBeforeForwardingWithCleanupOAuthCookiesDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.oauth2_cleanup_cookies", "false"}});

  // Initialize with use_refresh_token set to false
  init(getConfig(true /* forward_bearer_token */));

  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(0)));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=4TKyxPV/F7yyvr0XgJ2bkWFOc8t4IOFen1k29b84MAQ="},
      {Http::Headers::get().Cookie.get(), "OauthExpires=1600"},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "IdToken=" + TEST_ENCRYPTED_ID_TOKEN},
      {Http::Headers::get().Cookie.get(), "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN},
      {Http::Headers::get().Cookie.get(), "OauthNonce=" + TEST_CSRF_TOKEN},
  };

  // cookie-validation mocking
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));

  // return reference mocking
  std::string access_token{"access_code"};
  EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(access_token));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  // Expect the request headers to be updated with the decrypted tokens
  auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("BearerToken"), "access_code");
  EXPECT_EQ(cookies.at("IdToken"), "some-id-token");
  EXPECT_EQ(cookies.at("RefreshToken"), "some-refresh-token");
}

// Verifies that requests matching the pass_through_matcher configuration are not modified by the
// filter. The request headers and cookies remain unchanged, and only the oauth_passthrough metric
// is incremented. This ensures correct behavior when the filter is configured to skip processing
// for specific requests.
TEST_F(OAuth2Test, RequestIsUnchangedWhenPassThroughMatcherMatches) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Options},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=some_oauth_hmac_value"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=some_oauth_expires_value"},
      {Http::Headers::get().Cookie.get(), "RefreshToken=some_refresh_token_value"},
      {Http::Headers::get().Cookie.get(), "OauthNonce=some_oauth_nonce_value"},
      {Http::Headers::get().Cookie.get(), "CodeVerifier=some_code_verifier_value"}};

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/anypath"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Options},
      {Http::Headers::get().Cookie.get(), "OauthHMAC=some_oauth_hmac_value"},
      {Http::Headers::get().Cookie.get(), "OauthExpires=some_oauth_expires_value"},
      {Http::Headers::get().Cookie.get(), "RefreshToken=some_refresh_token_value"},
      {Http::Headers::get().Cookie.get(), "OauthNonce=some_oauth_nonce_value"},
      {Http::Headers::get().Cookie.get(), "CodeVerifier=some_code_verifier_value"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(request_headers, expected_headers);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_failure").value(), 0);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_passthrough").value(), 1);
  EXPECT_EQ(scope_.counterFromString("test.my_prefix.oauth_success").value(), 0);
}

// Verify cookie prefixes "__Secure-" and "__Host-" cause addition of the "Secure" attribute at
// sign out.
TEST_F(OAuth2Test, SecureAttributeAddedForSecureCookiePrefixesOnSignout) {
  auto make_config =
      [&](absl::string_view prefix) -> envoy::extensions::filters::http::oauth2::v3::OAuth2Config {
    envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
    auto* endpoint = p.mutable_token_endpoint();
    endpoint->set_cluster("auth.example.com");
    endpoint->set_uri("auth.example.com/_oauth");
    p.set_authorization_endpoint("https://auth2.example.com/oauth/authorize/");
    p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
    p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
    auto* credentials = p.mutable_credentials();
    credentials->set_client_id(TEST_CLIENT_ID);
    credentials->mutable_token_secret()->set_name("secret");
    credentials->mutable_hmac_secret()->set_name("hmac");
    auto* cookie_names = credentials->mutable_cookie_names();
    cookie_names->set_oauth_hmac(absl::StrCat(prefix, "OauthHMAC"));
    cookie_names->set_bearer_token(absl::StrCat(prefix, "BearerToken"));
    cookie_names->set_id_token(absl::StrCat(prefix, "IdToken"));
    cookie_names->set_refresh_token(absl::StrCat(prefix, "RefreshToken"));

    auto* matcher = p.add_pass_through_matcher();
    matcher->set_name(":method");
    matcher->mutable_string_match()->set_exact("OPTIONS");

    return p;
  };

  auto run_test_with_prefix = [&](absl::string_view prefix, bool expect_secure) {
    auto p = make_config(prefix);
    auto secret_reader = std::make_shared<MockSecretReader>();
    init(std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_, secret_reader,
                                        scope_, "test."));

    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
        .WillOnce(Invoke([&](Http::ResponseHeaderMap& passed_headers, bool) {
          EXPECT_EQ(passed_headers.get(Http::Headers::get().SetCookie).size(), 6);
          const auto& cookie_str =
              passed_headers.get(Http::Headers::get().SetCookie)[0]->value().getStringView();
          if (expect_secure) {
            EXPECT_THAT(cookie_str, testing::HasSubstr("; Secure"));
          } else {
            EXPECT_THAT(cookie_str, testing::Not(testing::HasSubstr("; Secure")));
          }
        }));
    auto request_headers = Http::TestRequestHeaderMapImpl{
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Path.get(), "/_signout"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
    };
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, false));
  };

  run_test_with_prefix("__Secure-", true);
  run_test_with_prefix("__Host-", true);
  run_test_with_prefix("", false);
}

/**
 * Scenario: decryptToken is called with an empty string.
 * Expected behavior: returns empty string.
 */
TEST_F(OAuth2Test, DecryptTokenEmpty) { EXPECT_EQ(decryptTokenForTest(""), ""); }

/**
 * Scenario: decryptToken is called with a ciphertext that was encrypted with the same HMAC secret.
 * Expected behavior: returns the original plaintext.
 */
TEST_F(OAuth2Test, DecryptTokenSameSecret) {
  const std::string plaintext = "some_access_token_value";
  const std::string ciphertext = encryptTokenForTest(plaintext);
  EXPECT_EQ(decryptTokenForTest(ciphertext), plaintext);
}

/**
 * Scenario: decryptToken is called with a token that is not a valid AES ciphertext (e.g. a
 * legacy unencrypted token or a ciphertext from a different key where PKCS#7 padding fails).
 * Expected behavior: returns the original input unchanged and emits an error log.
 */
TEST_F(OAuth2Test, DecryptTokenDecryptionFails) {
  // This looks like a bearer token but is not valid AES-CBC ciphertext encrypted under the
  // filter's HMAC secret. EVP_DecryptFinal_ex will reject the PKCS#7 padding.
  const std::string unencrypted = "j5Vhtnz_uyhDVTrSri3GzLoroprQYVoXsp61kIq_JC4";
  EXPECT_LOG_CONTAINS("error", "failed to decrypt token",
                      { EXPECT_EQ(decryptTokenForTest(unencrypted), unencrypted); });
}

/**
 * Scenario: decryptToken is called with a ciphertext that, when AES-CBC decrypted under the
 * filter's HMAC secret, passes the PKCS#7 padding check but produces bytes that are not valid
 * HTTP header field values (e.g. null bytes, control characters).
 *
 * This is the "spurious success" path: PKCS#7 padding validation passes with ~1/256 probability
 * when decrypting ciphertext under the wrong key, but the resulting "plaintext" is binary garbage.
 * The same path is reachable by explicitly encrypting a binary string with the correct key.
 *
 * Before the fix: decryptToken returns the garbage plaintext, which later crashes Envoy with
 * HeaderStringValidator::assertValid() when the value is used in a Cookie header.
 * After the fix: decryptToken detects the invalid plaintext and returns the original ciphertext.
 */
TEST_F(OAuth2Test, DecryptTokenSpuriousSuccessReturnsOriginalInput) {
  // Encrypt a string containing bytes that are not valid HTTP header field values
  // (null byte, control chars, high bytes). This ciphertext decrypts correctly under the filter's
  // key, so EVP_DecryptFinal_ex succeeds, but the resulting plaintext fails headerValueIsValid.
  const std::string binary_plaintext("\x00\x01\x02\xff", 4);
  const std::string ciphertext = encryptTokenForTest(binary_plaintext);
  // Tighter assertion: match on the message that is unique to the new headerValueIsValid branch
  // ("plaintext is not a valid header value"), rather than the generic "failed to decrypt token"
  // prefix that is shared with the pre-existing EVP_DecryptFinal_ex failure path. A regression
  // that re-introduced the crash via the old error path would emit a different message and fail
  // this test.
  EXPECT_LOG_CONTAINS("error", "plaintext is not a valid header value",
                      { EXPECT_EQ(decryptTokenForTest(ciphertext), ciphertext); });
}

/**
 * Scenario: A request arrives with a RefreshToken cookie whose AES-CBC decryption "succeeds"
 * (PKCS#7 padding is valid) but produces binary garbage that is not a valid HTTP header value.
 * This can happen when the HMAC secret has been rotated and the old ciphertext accidentally passes
 * the padding check under the new key (~1/256 probability per token per request).
 *
 * Before the fix: decryptAndUpdateOAuthTokenCookies crashes with HeaderStringValidator::assertValid
 * because the binary plaintext is StrJoin-ed into the Cookie header value.
 * After the fix: decodeHeaders continues gracefully and returns a redirect to the OAuth server.
 */
TEST_F(OAuth2Test, GarbagePlaintextCookieDoesNotCrash) {
  // Create a ciphertext that decrypts (under the filter's key) to binary bytes that are not valid
  // HTTP header field values, simulating the spurious-success case.
  const std::string binary_plaintext("\x00\x01\x02\xff", 4);
  const std::string garbage_ciphertext = encryptTokenForTest(binary_plaintext);

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/original_path"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), "RefreshToken=" + garbage_ciphertext},
  };

  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));

  // Assert the redirect actually happened: filter took the redirect path (302 with Location)
  // rather than crashing or returning a 401. We only check Status=="302" and Location is set
  // to avoid brittleness from Set-Cookie values, nonce, and CSRF in the full header map.
  // redirectToOAuthServer calls decoder_callbacks_->encodeHeaders with end_stream=true.
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(testing::Truly([](const Http::ResponseHeaderMap& headers) {
                               return headers.getStatusValue() == "302" &&
                                      !headers.getLocationValue().empty();
                             }),
                             true));

  // Tighter assertion: match on the message that is unique to the new headerValueIsValid branch.
  // A regression that re-introduced the crash via the old EVP_DecryptFinal_ex-failure path would
  // emit a different message and cause this test to fail.
  // Before the fix this line would trigger a crash (ENVOY_BUG assertion in HeaderStringValidator).
  // After the fix it should complete without crashing and redirect to the OAuth server.
  EXPECT_LOG_CONTAINS("error", "plaintext is not a valid header value", {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, false));
  });

  // After the fix, the Cookie header should contain the original (valid ASCII) ciphertext,
  // not the binary garbage that decryption produced.
  auto cookies = Http::Utility::parseCookies(request_headers);
  // Use ASSERT_TRUE first so the test fails clearly (not with out_of_range) if the cookie were
  // dropped instead of preserved.
  ASSERT_TRUE(cookies.contains("RefreshToken"));
  EXPECT_EQ(cookies.at("RefreshToken"), garbage_ciphertext);
}

// Legacy AES-256-CBC ciphertext for the plaintext "access_code". Used by the rolling-upgrade tests
// below to exercise the CBC fallback path with a stable, known-CBC fixture (the production
// encrypt() may produce either GCM or CBC depending on the runtime flag, so we can't rely on
// encryptTokenForTest() to generate a CBC ciphertext deterministically).
static const std::string TEST_LEGACY_CBC_ENCRYPTED_ACCESS_TOKEN =
    "Fc1bBwAAAAAVzVsHAAAAAHDCo6XWwdgw5IYsxjfymIQ";
static const std::string TEST_LEGACY_CBC_PLAINTEXT = "access_code";

// AES-256-GCM ciphertext for the plaintext "access_code" with a 12-byte zero IV. The IV is fixed to
// keep this fixture stable; the production encrypt() uses a random IV. Used as a known-GCM fixture
// so the positive GCM path can be exercised without depending on the runtime flag dispatch in
// encryptTokenForTest().
static const std::string TEST_KNOWN_GCM_ENCRYPTED_ACCESS_TOKEN =
    "gcm.AAAAAAAAAAAAAAAAXKvqQaYSMR-WpXHLNyAmsF4A95dUO3Xxogek";

/**
 * Test fixture parameterized on whether envoy.reloadable_features.oauth2_use_gcm_encryption is
 * enabled. Each TEST_P below runs twice: once with the flag at the default (false, ``encrypt()``
 * emits AES-256-CBC) and once with the flag turned on (true, ``encrypt()`` emits AES-256-GCM
 * with the ``gcm.`` marker). The behaviors asserted here must hold in both cipher modes.
 */
class OAuth2CipherModeTest : public OAuth2Test, public ::testing::WithParamInterface<bool> {
public:
  void SetUp() override {
    if (useGcm()) {
      scoped_runtime_.mergeValues(
          {{"envoy.reloadable_features.oauth2_use_gcm_encryption", "true"}});
    }
  }
  bool useGcm() const { return GetParam(); }

private:
  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(BothCiphers, OAuth2CipherModeTest, ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "GcmEncryption" : "CbcEncryption";
                         });

/**
 * In both cipher modes, a legacy AES-256-CBC ciphertext must be accepted via the CBC fallback,
 * since envoy.reloadable_features.oauth2_legacy_cbc_decrypt_compat defaults to true during the
 * migration window. Turning on GCM encryption must not break decryption of cookies issued by
 * older instances that are still emitting CBC.
 */
TEST_P(OAuth2CipherModeTest, LegacyCbcCookieIsAccepted) {
  EXPECT_EQ(0, config_->stats().oauth_legacy_cbc_decrypt_.value());
  EXPECT_EQ(decryptTokenForTest(TEST_LEGACY_CBC_ENCRYPTED_ACCESS_TOKEN), TEST_LEGACY_CBC_PLAINTEXT);
  // Successful CBC fallback must tick oauth_legacy_cbc_decrypt so operators can observe
  // legacy cookie traffic and know when it's safe to flip oauth2_legacy_cbc_decrypt_compat off.
  EXPECT_EQ(1, config_->stats().oauth_legacy_cbc_decrypt_.value());
}

/**
 * In both cipher modes, a valid "gcm."-prefixed ciphertext must decrypt to its original
 * plaintext via the unconditional GCM path, and the CBC fallback log must NOT fire.
 * This guards the post-CVE-2026-47775 happy path against
 * any future flag-coupled regression in decrypt().
 */
TEST_P(OAuth2CipherModeTest, KnownGcmCookieIsAccepted) {
  EXPECT_EQ(decryptTokenForTest(TEST_KNOWN_GCM_ENCRYPTED_ACCESS_TOKEN), TEST_LEGACY_CBC_PLAINTEXT);
  // GCM path must never increment the legacy CBC counter.
  EXPECT_EQ(0, config_->stats().oauth_legacy_cbc_decrypt_.value());
}

/**
 * In both cipher modes, encrypt()'s output must round-trip back through decrypt() to the
 * original plaintext, and the wire format must match the mode: no marker when use_gcm=false
 * (legacy CBC), "gcm." marker when use_gcm=true. The CBC fallback log must not fire on the
 * GCM round trip.
 */
TEST_P(OAuth2CipherModeTest, EncryptThenDecryptRoundTrips) {
  const std::string plaintext = "some_access_token_value";
  const std::string ciphertext = encryptTokenForTest(plaintext);
  EXPECT_EQ(absl::StartsWith(ciphertext, "gcm."), useGcm())
      << "encrypt() wire format does not match oauth2_use_gcm_encryption=" << useGcm();
  if (useGcm()) {
    EXPECT_EQ(decryptTokenForTest(ciphertext), plaintext);
    EXPECT_EQ(0, config_->stats().oauth_legacy_cbc_decrypt_.value());
  } else {
    EXPECT_EQ(decryptTokenForTest(ciphertext), plaintext);
    EXPECT_EQ(1, config_->stats().oauth_legacy_cbc_decrypt_.value());
  }
}

/**
 * In both cipher modes, attacker garbage (not valid GCM nor valid CBC) must fail cleanly:
 * decryptToken returns the original input and no crash. Flipping use_gcm must not open a door
 * for attacker-chosen plaintext on totally invalid input.
 */
TEST_P(OAuth2CipherModeTest, GarbageCiphertextFailsCleanly) {
  // Length > 12 + 16 so the GCM-side size check doesn't short-circuit; also > 16 so the CBC-side
  // size check doesn't either. Neither GCM tag nor CBC padding will validate, so both paths must
  // reject.
  const std::string garbage = "j5Vhtnz_uyhDVTrSri3GzLoroprQYVoXsp61kIq_JC4-extra-bytes";
  EXPECT_LOG_CONTAINS("error", "failed to decrypt token",
                      { EXPECT_EQ(decryptTokenForTest(garbage), garbage); });
}

/**
 * In both cipher modes, a "gcm."-prefixed ciphertext whose decoded payload is too short to
 * hold IV (12) + tag (16) must fail at the size check inside decrypt() before reaching
 * EVP_DecryptFinal_ex. decryptToken returns the original input and logs an error. The cipher
 * mode parameter is irrelevant for this path (decrypt() dispatches on the marker, not the
 * flag) but we run under both to guard against future flag-coupled bugs.
 */
TEST_P(OAuth2CipherModeTest, GcmPrefixedGarbageBelowSizeBoundFailsCleanly) {
  // 28-char base64url body decodes to 21 bytes, which is <= the 12 + 16 = 28 minimum the size
  // check enforces. Must be rejected without touching the EVP code path.
  const std::string garbage = "gcm.AAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  EXPECT_LOG_CONTAINS("error", "failed to decrypt token",
                      { EXPECT_EQ(decryptTokenForTest(garbage), garbage); });
}

/**
 * In both cipher modes, a "gcm."-prefixed ciphertext that passes the size check but whose
 * auth tag does not match must fail at EVP_DecryptFinal_ex. decryptToken returns the original
 * input and logs an error. This is the critical security path: flipping use_gcm must never let
 * an unauthenticated GCM ciphertext through.
 */
TEST_P(OAuth2CipherModeTest, GcmPrefixedGarbageAboveSizeBoundFailsCleanly) {
  // 48-char base64url body decodes to 36 bytes (> 28), so the size check passes and the tag
  // check is what must reject. All-zero bytes will not match the tag.
  const std::string garbage = "gcm.AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  EXPECT_LOG_CONTAINS("error", "failed to decrypt token",
                      { EXPECT_EQ(decryptTokenForTest(garbage), garbage); });
}

/**
 * Post-migration state (use_gcm=true, compat=false): only GCM-marked cookies decrypt; legacy
 * CBC cookies are rejected. This is the configuration operators land on after their cookie TTL
 * has elapsed and is what fully closes CVE-2026-47775. Not parameterized — this scenario is
 * specific to the compat-off side of the matrix.
 */
TEST_F(OAuth2Test, PostMigrationConfigRejectsCbcCiphertext) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_use_gcm_encryption", "true"},
       {"envoy.reloadable_features.oauth2_legacy_cbc_decrypt_compat", "false"}});
  EXPECT_LOG_CONTAINS("error", "failed to decrypt token", {
    EXPECT_EQ(decryptTokenForTest(TEST_LEGACY_CBC_ENCRYPTED_ACCESS_TOKEN),
              TEST_LEGACY_CBC_ENCRYPTED_ACCESS_TOKEN);
  });
  // Rejection path (no marker + compat off) must not tick the legacy CBC counter: nothing was
  // decrypted, just refused.
  EXPECT_EQ(0, config_->stats().oauth_legacy_cbc_decrypt_.value());
}

TEST_F(OAuth2Test, RequestSignoutWithTokenCookieChunkingDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "false"}});

  std::string long_token(5000, 'x');

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), "OauthNonce=csrf_token"},
      {Http::Headers::get().Cookie.get(), "CodeVerifier=code_verifier"},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("BearerToken_0=", long_token.substr(0, 3955))},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("BearerToken_1=", long_token.substr(3955, 5000))},
      {Http::Headers::get().Cookie.get(), "BearerToken_chunks=2"},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ(headers.Status()->value(), "302");
        EXPECT_EQ(headers.get(Http::Headers::get().SetCookie).size(), 9);
        EXPECT_EQ(headers.get(Http::Headers::get().Location)[0]->value().getStringView(),
                  "https://traffic.example.com/");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "OauthHMAC"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_0"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_1"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_chunks"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "IdToken"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "RefreshToken"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "OauthNonce"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "CodeVerifier"), "deleted");
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

TEST_F(OAuth2Test, OAuthAccessTokenSucessWithChunkedToken) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "true"}});

  std::string long_token(5000, 'x');
  oauthHMAC = "70RVEPVYna4P+pWr7p9ILsA9RobsKbFUiuMp5FGpAm4=;";
  // Set SystemTime to a fixed point so we get consistent HMAC encodings between test runs.
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  // host_ must be set, which is guaranteed (ASAN).
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  // Expected response after the callback is complete.
  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       absl::StrCat("BearerToken_0=", TEST_ENCRYPTED_ACCESS_TOKEN_CHUNK_1,
                    ";path=/;Max-Age=600;secure;HttpOnly")},
      {Http::Headers::get().SetCookie.get(),
       absl::StrCat("BearerToken_1=", TEST_ENCRYPTED_ACCESS_TOKEN_CHUNK_2,
                    ";path=/;Max-Age=600;secure;HttpOnly")},
      {Http::Headers::get().SetCookie.get(),
       "BearerToken_chunks=2;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess(long_token, "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_chunked_.value());
}

// With the guard off, a token too large for one cookie is written as one oversized cookie, as
// before chunking.
TEST_F(OAuth2Test, OAuthAccessTokenSucessWithChunkedTokenDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "false"}});

  std::string long_token(5000, 'x');
  oauthHMAC = "70RVEPVYna4P+pWr7p9ILsA9RobsKbFUiuMp5FGpAm4=;";
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
  };
  filter_->decodeHeaders(request_headers, false);

  Http::TestRequestHeaderMapImpl expected_headers{
      {Http::Headers::get().Status.get(), "302"},
      {Http::Headers::get().SetCookie.get(),
       "OauthHMAC=" + oauthHMAC + "path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       "OauthExpires=1600;path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().SetCookie.get(),
       absl::StrCat("BearerToken=", TEST_ENCRYPTED_ACCESS_TOKEN_CHUNK_1,
                    TEST_ENCRYPTED_ACCESS_TOKEN_CHUNK_2, ";path=/;Max-Age=600;secure;HttpOnly")},
      {Http::Headers::get().SetCookie.get(),
       "IdToken=" + TEST_ENCRYPTED_ID_TOKEN + ";path=/;Max-Age=600;secure;HttpOnly"},
      {Http::Headers::get().Location.get(), ""},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));

  filter_->onGetAccessTokenSuccess(long_token, "some-id-token", "some-refresh-token",
                                   std::chrono::seconds(600));
}

// Enabling the guard must not change anything for a cookie that already fits. This pins the exact
// boundary: at the size limit the output is identical either way, and only one byte past it does
// chunking start. Encryption is off so the cookie value is the token itself and the math is exact.
TEST_F(OAuth2Test, TokenCookieAtSizeLimitIsNotChunked) {
  constexpr auto DisabledSameSite = ::envoy::extensions::filters::http::oauth2::v3::
      CookieConfig_SameSite::CookieConfig_SameSite_DISABLED;

  auto emit = [&](bool chunking_enabled, const std::string& access_token) {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({{"envoy.reloadable_features.oauth2_chunk_large_token_cookies",
                                 chunking_enabled ? "true" : "false"}});
    init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                   ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                       OAuth2Config_AuthType_URL_ENCODED_BODY,
                   0, false, false, false, false, false, DisabledSameSite, DisabledSameSite,
                   DisabledSameSite, DisabledSameSite, DisabledSameSite, DisabledSameSite,
                   DisabledSameSite, 0, 0, true /* disable_token_encryption */));
    test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

    Http::TestRequestHeaderMapImpl request_headers{
        {Http::Headers::get().Path.get(), "/original_path"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Scheme.get(), "https"},
    };
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));
    std::string legit_token{"legit_token"};
    EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));
    filter_->decodeHeaders(request_headers, false);

    std::vector<std::string> cookies;
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
        .WillOnce(Invoke([&cookies](Http::ResponseHeaderMap& headers, bool) {
          headers.iterate([&cookies](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
            if (header.key().getStringView() == Http::Headers::get().SetCookie.get()) {
              cookies.push_back(std::string(header.value().getStringView()));
            }
            return Http::HeaderMap::Iterate::Continue;
          });
        }));
    filter_->onGetAccessTokenSuccess(access_token, "", "", std::chrono::seconds(600));
    return cookies;
  };

  auto bearer_cookie = [](const std::vector<std::string>& cookies) {
    for (const auto& cookie : cookies) {
      if (absl::StartsWith(cookie, "BearerToken=")) {
        return cookie;
      }
    }
    return std::string{};
  };

  // Measure everything the cookie carries besides the value.
  const std::string probe = bearer_cookie(emit(false, "x"));
  ASSERT_FALSE(probe.empty());
  const size_t overhead = probe.size() - 1;
  ASSERT_LT(overhead, MaxCookieSize);

  // A token that fills the cookie exactly to the limit must be written the same way either way.
  const std::string exact_fit(MaxCookieSize - overhead, 'a');
  const std::vector<std::string> without_guard = emit(false, exact_fit);
  const std::vector<std::string> with_guard = emit(true, exact_fit);
  EXPECT_EQ(without_guard, with_guard);
  EXPECT_EQ(bearer_cookie(with_guard).size(), MaxCookieSize);

  // One byte past the limit is where chunking starts.
  const std::string one_too_big(MaxCookieSize - overhead + 1, 'a');
  const std::vector<std::string> over = emit(true, one_too_big);
  // The base cookie is either absent or expired, never carrying the value.
  const std::string over_base = bearer_cookie(over);
  EXPECT_TRUE(over_base.empty() || absl::StartsWith(over_base, "BearerToken=deleted"));
  bool has_chunk_count = false;
  for (const auto& cookie : over) {
    if (absl::StartsWith(cookie, "BearerToken_chunks=")) {
      has_chunk_count = true;
    }
  }
  EXPECT_TRUE(has_chunk_count);
}

TEST_F(OAuth2Test, SetTokenCookie) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "true"}});
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));

  // Helper function to create tokens of specific sizes
  auto createToken = [](size_t size, char fill_char) { return std::string(size, fill_char); };

  // Test mixed token sizes: single cookie, chunked, and empty.
  {
    // Access token: Small enough to fit in a single cookie
    std::string access_token = createToken(100, 'a');

    // ID token: Large enough to require chunking
    std::string id_token = createToken(5000, 'i');

    std::string refresh_token;

    Http::TestRequestHeaderMapImpl request_headers{
        {Http::Headers::get().Path.get(), "/original_path?var1=1&var2=2"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Scheme.get(), "https"},
    };

    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));
    std::string legit_token{"legit_token"};
    EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));
    filter_->decodeHeaders(request_headers, false);

    Http::TestResponseHeaderMapImpl response_headers;
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
        .WillOnce(Invoke([&response_headers](Http::ResponseHeaderMap& headers, bool) {
          response_headers = Http::TestResponseHeaderMapImpl(headers);
        }));

    filter_->onGetAccessTokenSuccess(access_token, id_token, refresh_token,
                                     std::chrono::seconds(600));

    auto extract_cookies = [](const Http::TestResponseHeaderMapImpl& headers) {
      std::vector<std::string> cookies;
      headers.iterate([&cookies](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
        if (header.key().getStringView() == Http::Headers::get().SetCookie.get()) {
          cookies.push_back(std::string(header.value().getStringView()));
        }
        return Http::HeaderMap::Iterate::Continue;
      });
      return cookies;
    };
    auto cookies = extract_cookies(response_headers);

    // Count cookies by type
    int bearer_token_count = 0;
    int id_token_chunk_count = 0;
    int id_token_chunks_count = 0;
    int refresh_token_count = 0;

    for (const auto& cookie : cookies) {
      if (cookie.find("BearerToken=") == 0) {
        bearer_token_count++;
      } else if (cookie.find("IdToken_0=") == 0) {
        id_token_chunk_count++;
      } else if (cookie.find("IdToken_1=") == 0) {
        id_token_chunk_count++;
      } else if (cookie.find("IdToken_chunks=2") == 0) {
        id_token_chunks_count++;
      } else if (cookie.find("RefreshToken") == 0) {
        refresh_token_count++;
      }
    }

    // Verify expected cookie counts
    EXPECT_EQ(bearer_token_count, 1) << "Expected exactly 1 BearerToken cookie";
    EXPECT_EQ(id_token_chunk_count, 2)
        << "Expected exactly 2 IdToken chunk cookies (IdToken_0 and IdToken_1)";
    EXPECT_EQ(id_token_chunks_count, 1) << "Expected exactly 1 IdToken_chunks cookie";
    EXPECT_EQ(refresh_token_count, 0) << "Expected no RefreshToken cookies for an empty token";
  }
}

TEST_F(OAuth2Test, EncryptedChunkedCookieRoundTripUsesRealValidator) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "true"}});
  initWithRealValidator(getConfig(true /* forward_bearer_token */));
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));

  Http::TestRequestHeaderMapImpl initial_request{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(initial_request, false));

  Http::TestResponseHeaderMapImpl login_response;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&login_response](Http::ResponseHeaderMap& headers, bool) {
        login_response = Http::TestResponseHeaderMapImpl(headers);
      }));
  const std::string access_token(5000, 'a');
  filter_->onGetAccessTokenSuccess(access_token, "", "", std::chrono::seconds(600));
  EXPECT_FALSE(Http::Utility::parseSetCookieValue(login_response, "BearerToken_chunks").empty());

  const std::string cookie_header = cookieHeaderFromResponse(login_response);
  initWithRealValidator(getConfig(true /* forward_bearer_token */));
  Http::TestRequestHeaderMapImpl authenticated_request{
      {Http::Headers::get().Path.get(), "/resource"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), cookie_header},
  };

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(authenticated_request, false));
  EXPECT_EQ(authenticated_request.getInline(authorization_handle.handle())->value().getStringView(),
            absl::StrCat("Bearer ", access_token));
  const absl::string_view upstream_cookies =
      authenticated_request.get(Http::Headers::get().Cookie)[0]->value().getStringView();
  EXPECT_EQ(upstream_cookies.find("BearerToken_"), absl::string_view::npos);
  EXPECT_EQ(upstream_cookies.find("BearerToken_chunks"), absl::string_view::npos);
}

TEST_F(OAuth2Test, RuntimeRollbackReplacesChunkedCookie) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "false"}});
  init(getConfig(false /* forward_bearer_token */));
  const size_t split = TEST_ENCRYPTED_ACCESS_TOKEN.size() / 2;
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/resource"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("BearerToken_0=", TEST_ENCRYPTED_ACCESS_TOKEN.substr(0, split))},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("BearerToken_1=", TEST_ENCRYPTED_ACCESS_TOKEN.substr(split))},
      {Http::Headers::get().Cookie.get(), "BearerToken_chunks=2"},
  };
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_0"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_1"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_chunks"), "deleted");
        // With the guard off, a token too large for one cookie is still written as one cookie.
        const std::string bearer = Http::Utility::parseSetCookieValue(headers, "BearerToken");
        EXPECT_GT(bearer.size(), MaxCookieSize);
      }));
  // Large enough that it would be chunked with the guard on.
  filter_->onGetAccessTokenSuccess(std::string(5000, 'x'), "", "", std::chrono::seconds(600));
  EXPECT_EQ(0, config_->stats().oauth_token_cookie_chunked_.value());
}

TEST_F(OAuth2Test, RefreshFlowPreservesNormalizedChunkedRefreshToken) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  const size_t split = TEST_ENCRYPTED_REFRESH_TOKEN.size() / 2;
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/resource"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("RefreshToken_0=", TEST_ENCRYPTED_REFRESH_TOKEN.substr(0, split))},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("RefreshToken_1=", TEST_ENCRYPTED_REFRESH_TOKEN.substr(split))},
      {Http::Headers::get().Cookie.get(), "RefreshToken_chunks=2"},
  };
  std::string refresh_token{"some-refresh-token"};
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(refresh_token));
  EXPECT_CALL(*oauth_client_, asyncRefreshAccessToken(refresh_token, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->onRefreshAccessTokenSuccess("new-access-token", "", "", std::chrono::seconds(600));
  EXPECT_EQ(refreshTokenForTest(), refresh_token);
  const absl::string_view upstream_cookies =
      request_headers.get(Http::Headers::get().Cookie)[0]->value().getStringView();
  EXPECT_EQ(upstream_cookies.find("RefreshToken"), absl::string_view::npos);
}

TEST_F(OAuth2Test, OversizedTokenStopsBeforeSettingSessionCookies) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "true"}});
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
  };
  filter_->decodeHeaders(request_headers, false);

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_TRUE(headers.get(Http::Headers::get().SetCookie).empty());
      }));
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::ServiceUnavailable, "Service Unavailable", _, _, _));
  filter_->onGetAccessTokenSuccess(std::string(MaxCookieSize * 15, 'x'), "", "",
                                   std::chrono::seconds(600));
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_oversized_.value());
}

TEST_F(OAuth2Test, RequestSignoutWithMalformedChunkCount) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), "BearerToken_chunks=invalid"},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        // Without a valid count every index a chunk may use is deleted, with the count cookie.
        for (size_t index = 0; index < MaxChunksPerToken; ++index) {
          EXPECT_EQ(
              Http::Utility::parseSetCookieValue(headers, absl::StrCat("BearerToken_", index)),
              "deleted");
        }
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_chunks"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken"), "deleted");
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

// Replacing a chunked token with a smaller chunked token must not send a delete and a set for the
// same cookie name. Only the stale chunks beyond the new count are deleted.
TEST_F(OAuth2Test, RechunkedTokenOnlyDeletesStaleChunks) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "true"}});
  init(getConfig(false /* forward_bearer_token */));
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/resource"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), "BearerToken_0=a"},
      {Http::Headers::get().Cookie.get(), "BearerToken_1=b"},
      {Http::Headers::get().Cookie.get(), "BearerToken_2=c"},
      {Http::Headers::get().Cookie.get(), "BearerToken_chunks=3"},
  };
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        absl::flat_hash_map<std::string, int> name_counts;
        headers.iterate(
            [&name_counts](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
              if (header.key().getStringView() == Http::Headers::get().SetCookie.get()) {
                const std::vector<absl::string_view> name_value =
                    absl::StrSplit(header.value().getStringView(), absl::MaxSplits('=', 1));
                name_counts[std::string(name_value[0])]++;
              }
              return Http::HeaderMap::Iterate::Continue;
            });
        for (const auto& [name, count] : name_counts) {
          EXPECT_EQ(count, 1) << name;
        }
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_chunks"), "2");
        EXPECT_NE(Http::Utility::parseSetCookieValue(headers, "BearerToken_0"), "deleted");
        EXPECT_NE(Http::Utility::parseSetCookieValue(headers, "BearerToken_1"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_2"), "deleted");
      }));
  filter_->onGetAccessTokenSuccess(std::string(5000, 'x'), "", "", std::chrono::seconds(600));
}

// When a chunked token is not replaced by a new one, the chunks recorded while rebuilding the
// request cookies must be deleted along with the base cookie.
TEST_F(OAuth2Test, ChunkedTokenWithoutReplacementIsFullyDeleted) {
  init(getConfig(false /* forward_bearer_token */));
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
  const size_t split = TEST_ENCRYPTED_ID_TOKEN.size() / 2;
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/resource"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("IdToken_0=", TEST_ENCRYPTED_ID_TOKEN.substr(0, split))},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("IdToken_1=", TEST_ENCRYPTED_ID_TOKEN.substr(split))},
      {Http::Headers::get().Cookie.get(), "IdToken_chunks=2"},
  };
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        // The client only had the chunks, so there is no single cookie to delete.
        EXPECT_TRUE(Http::Utility::parseSetCookieValue(headers, "IdToken").empty());
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "IdToken_0"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "IdToken_1"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "IdToken_chunks"), "deleted");
      }));
  // No new ID token, so the existing chunked one is deleted.
  filter_->onGetAccessTokenSuccess("new-access-token", "", "", std::chrono::seconds(600));
}

// The size check uses an upper bound of the encrypted size instead of encrypting. It must never be
// smaller than what encryptToken() produces, with either cipher.
TEST_F(OAuth2Test, MaxEncryptedTokenSizeCoversEncryptedToken) {
  for (const bool use_gcm : {false, true}) {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues(
        {{"envoy.reloadable_features.oauth2_use_gcm_encryption", use_gcm ? "true" : "false"}});
    std::vector<size_t> sizes;
    for (size_t size = 0; size <= 100; ++size) {
      sizes.push_back(size);
    }
    sizes.insert(sizes.end(), {1000, 4096, 12345, 20000});
    for (const size_t size : sizes) {
      const size_t encrypted_size = encryptTokenForTest(std::string(size, 'x')).size();
      const size_t bound = maxEncryptedTokenSizeForTest(size);
      EXPECT_GE(bound, encrypted_size) << "gcm=" << use_gcm << " size=" << size;
      // The bound only over-estimates by a few bytes.
      EXPECT_LE(bound - encrypted_size, 22) << "gcm=" << use_gcm << " size=" << size;
    }
  }
}

// cookieTailSize() must match the tail BuildCookieTail() builds, for every cookie type.
TEST_F(OAuth2Test, CookieTailSizeMatchesBuiltTail) {
  constexpr auto StrictSameSite = ::envoy::extensions::filters::http::oauth2::v3::
      CookieConfig_SameSite::CookieConfig_SameSite_STRICT;
  constexpr auto DisabledSameSite = ::envoy::extensions::filters::http::oauth2::v3::
      CookieConfig_SameSite::CookieConfig_SameSite_DISABLED;
  for (const bool set_cookie_domain : {false, true}) {
    init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                   ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                       OAuth2Config_AuthType_URL_ENCODED_BODY,
                   0, false, false, set_cookie_domain, false, false, StrictSameSite,
                   DisabledSameSite, StrictSameSite, DisabledSameSite, StrictSameSite,
                   DisabledSameSite, DisabledSameSite));
    setCookieExpiresForTest("600", "3600", "604800");
    for (int cookie_type = 1; cookie_type <= 5; ++cookie_type) {
      EXPECT_EQ(cookieTailSizeForTest(cookie_type), buildCookieTailForTest(cookie_type).size())
          << "domain=" << set_cookie_domain << " type=" << cookie_type;
    }
  }
}

// Chunks do not overwrite a single cookie of the same token, so a single cookie on the request is
// deleted when the new value is chunked.
TEST_F(OAuth2Test, ChunkedTokenDeletesSingleCookie) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "true"}});
  init(getConfig(false /* forward_bearer_token */));
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/resource"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
  };
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "BearerToken_chunks"), "2");
        EXPECT_FALSE(Http::Utility::parseSetCookieValue(headers, "BearerToken_0").empty());
        EXPECT_FALSE(Http::Utility::parseSetCookieValue(headers, "BearerToken_1").empty());
      }));
  filter_->onGetAccessTokenSuccess(std::string(5000, 'x'), "", "", std::chrono::seconds(600));
}

// Sign out sees the token cookies after they are rejoined and decrypted, so the end session
// endpoint gets the ID token itself, and the chunks of the ID token are deleted too.
TEST_F(OAuth2Test, RequestSignoutSendsDecryptedChunkedIdTokenHint) {
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  endpoint->mutable_timeout()->set_seconds(1);
  p.set_redirect_uri("%REQ(:scheme)%://%REQ(:authority)%" + TEST_CALLBACK);
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  auto credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");
  p.set_end_session_endpoint("https://auth.example.com/oauth/logout");
  p.add_auth_scopes("openid");
  auto secret_reader = std::make_shared<MockSecretReader>();
  init(std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_, secret_reader,
                                      scope_, "test."));

  const size_t split = TEST_ENCRYPTED_ID_TOKEN.size() / 2;
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("IdToken_0=", TEST_ENCRYPTED_ID_TOKEN.substr(0, split))},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("IdToken_1=", TEST_ENCRYPTED_ID_TOKEN.substr(split))},
      {Http::Headers::get().Cookie.get(), "IdToken_chunks=2"},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ(headers.getLocationValue(),
                  "https://auth.example.com/oauth/logout?id_token_hint=some-id-token&client_id=1&"
                  "post_logout_redirect_uri=https%3A%2F%2Ftraffic.example.com%2F");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "IdToken"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "IdToken_0"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "IdToken_1"), "deleted");
        EXPECT_EQ(Http::Utility::parseSetCookieValue(headers, "IdToken_chunks"), "deleted");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

// A refreshed token too large for the cookies is refused without continuing the request.
TEST_F(OAuth2Test, RefreshWithOversizedTokenIsRefused) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.oauth2_chunk_large_token_cookies", "true"}});
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/resource"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), "RefreshToken=" + TEST_ENCRYPTED_REFRESH_TOKEN},
  };
  std::string refresh_token{"some-refresh-token"};
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(refresh_token));
  EXPECT_CALL(*oauth_client_, asyncRefreshAccessToken(refresh_token, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::ServiceUnavailable, "Service Unavailable", _, _, _));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_TRUE(headers.get(Http::Headers::get().SetCookie).empty());
      }));
  filter_->onRefreshAccessTokenSuccess(std::string(MaxCookieSize * 15, 'x'), "", "",
                                       std::chrono::seconds(600));
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_oversized_.value());
}

// Without decryption and without chunks there is nothing to change, so the Cookie header is left
// exactly as the client sent it.
TEST_F(OAuth2Test, SingleTokenCookieIsNotRewrittenWithoutDecryption) {
  constexpr auto DisabledSameSite = ::envoy::extensions::filters::http::oauth2::v3::
      CookieConfig_SameSite::CookieConfig_SameSite_DISABLED;
  init(getConfig(true /* forward_bearer_token */, false /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, false, false, false, DisabledSameSite, DisabledSameSite,
                 DisabledSameSite, DisabledSameSite, DisabledSameSite, DisabledSameSite,
                 DisabledSameSite, 0, 0, true /* disable_token_encryption */));
  const std::string cookie_header = "b=2; BearerToken=abc; a=\"quoted\"";
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Cookie.get(), cookie_header},
  };
  decryptAndUpdateOAuthTokenCookiesForTest(request_headers);
  EXPECT_EQ(request_headers.get(Http::Headers::get().Cookie)[0]->value().getStringView(),
            cookie_header);
}

// A malformed chunk count falls back to the single cookie, and the chunk related cookies are
// removed before the request goes upstream.
TEST_F(OAuth2Test, MalformedChunkCountFallsBackToSingleCookie) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "BearerToken_chunks=abc"},
  };
  decryptAndUpdateOAuthTokenCookiesForTest(request_headers);

  const auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("BearerToken"), "access_code");
  EXPECT_FALSE(cookies.contains("BearerToken_chunks"));
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_malformed_chunks_.value());
}

// Envoy never writes a single chunk, so a count of 1 is malformed and the single cookie is used.
TEST_F(OAuth2Test, SingleChunkCountIsMalformed) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Cookie.get(), "BearerToken=" + TEST_ENCRYPTED_ACCESS_TOKEN},
      {Http::Headers::get().Cookie.get(), "BearerToken_0=chunk"},
      {Http::Headers::get().Cookie.get(), "BearerToken_chunks=1"},
  };
  decryptAndUpdateOAuthTokenCookiesForTest(request_headers);

  const auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("BearerToken"), "access_code");
  EXPECT_FALSE(cookies.contains("BearerToken_chunks"));
  // The fragment of the malformed set is not forwarded upstream.
  EXPECT_FALSE(cookies.contains("BearerToken_0"));
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_malformed_chunks_.value());
  EXPECT_EQ(0, config_->stats().oauth_token_cookie_reassembled_.value());
}

// Valid chunks are rejoined and decrypted under the token name.
TEST_F(OAuth2Test, ChunkedTokenIsRejoinedAndDecrypted) {
  const size_t split = TEST_ENCRYPTED_REFRESH_TOKEN.size() / 2;
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("RefreshToken_0=", TEST_ENCRYPTED_REFRESH_TOKEN.substr(0, split))},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("RefreshToken_1=", TEST_ENCRYPTED_REFRESH_TOKEN.substr(split))},
      {Http::Headers::get().Cookie.get(), "RefreshToken_chunks=2"},
      {Http::Headers::get().Cookie.get(), "other=1"},
  };
  decryptAndUpdateOAuthTokenCookiesForTest(request_headers);

  const auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("RefreshToken"), "some-refresh-token");
  EXPECT_EQ(cookies.at("other"), "1");
  EXPECT_FALSE(cookies.contains("RefreshToken_0"));
  EXPECT_FALSE(cookies.contains("RefreshToken_1"));
  EXPECT_FALSE(cookies.contains("RefreshToken_chunks"));
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_reassembled_.value());
}

// A chunk set missing a counted chunk yields no token, and its fragments are not forwarded
// upstream.
TEST_F(OAuth2Test, IncompleteChunkSetIsNotForwarded) {
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Cookie.get(), "other=1"},
      {Http::Headers::get().Cookie.get(), "RefreshToken_0=part0"},
      {Http::Headers::get().Cookie.get(), "RefreshToken_chunks=2"},
  };
  decryptAndUpdateOAuthTokenCookiesForTest(request_headers);

  const auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("other"), "1");
  EXPECT_FALSE(cookies.contains("RefreshToken"));
  EXPECT_FALSE(cookies.contains("RefreshToken_0"));
  EXPECT_FALSE(cookies.contains("RefreshToken_chunks"));
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_malformed_chunks_.value());
}

// After a refresh, encodeHeaders() writes the tokens. A large access token is chunked, and the
// refresh token recorded as chunks on the request is written back as a single cookie with its
// chunks deleted.
TEST_F(OAuth2Test, RefreshResponseChunksTokenAndDeletesRecordedChunks) {
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */));
  test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
  const size_t split = TEST_ENCRYPTED_REFRESH_TOKEN.size() / 2;
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/resource"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("RefreshToken_0=", TEST_ENCRYPTED_REFRESH_TOKEN.substr(0, split))},
      {Http::Headers::get().Cookie.get(),
       absl::StrCat("RefreshToken_1=", TEST_ENCRYPTED_REFRESH_TOKEN.substr(split))},
      {Http::Headers::get().Cookie.get(), "RefreshToken_chunks=2"},
  };
  std::string refresh_token{"some-refresh-token"};
  EXPECT_CALL(*validator_, setParams(_, _));
  EXPECT_CALL(*validator_, isValid()).WillOnce(Return(false));
  EXPECT_CALL(*validator_, canUpdateTokenByRefreshToken()).WillOnce(Return(true));
  EXPECT_CALL(*validator_, refreshToken()).WillRepeatedly(ReturnRef(refresh_token));
  EXPECT_CALL(*oauth_client_, asyncRefreshAccessToken(refresh_token, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->onRefreshAccessTokenSuccess(std::string(5000, 'x'), "", "", std::chrono::seconds(600));

  Http::TestResponseHeaderMapImpl response_headers{{Http::Headers::get().Status.get(), "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  // The access token is chunked. The client had no access token cookie, so there is none to delete.
  EXPECT_EQ(Http::Utility::parseSetCookieValue(response_headers, "BearerToken_chunks"), "2");
  EXPECT_FALSE(Http::Utility::parseSetCookieValue(response_headers, "BearerToken_0").empty());
  EXPECT_FALSE(Http::Utility::parseSetCookieValue(response_headers, "BearerToken_1").empty());
  EXPECT_TRUE(Http::Utility::parseSetCookieValue(response_headers, "BearerToken").empty());
  // The preserved refresh token fits in one cookie, so its recorded chunks are deleted.
  const std::string refresh_cookie =
      Http::Utility::parseSetCookieValue(response_headers, "RefreshToken");
  EXPECT_FALSE(refresh_cookie.empty());
  EXPECT_NE(refresh_cookie, "deleted");
  EXPECT_EQ(Http::Utility::parseSetCookieValue(response_headers, "RefreshToken_0"), "deleted");
  EXPECT_EQ(Http::Utility::parseSetCookieValue(response_headers, "RefreshToken_1"), "deleted");
  EXPECT_EQ(Http::Utility::parseSetCookieValue(response_headers, "RefreshToken_chunks"), "deleted");
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_chunked_.value());
}

// With token encryption disabled the chunks carry the token itself. A complete set is rejoined as
// is, and the fragments of an incomplete set are not forwarded upstream.
TEST_F(OAuth2Test, ChunksAreRejoinedWithoutDecryption) {
  constexpr auto DisabledSameSite = ::envoy::extensions::filters::http::oauth2::v3::
      CookieConfig_SameSite::CookieConfig_SameSite_DISABLED;
  init(getConfig(true /* forward_bearer_token */, true /* use_refresh_token */,
                 ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                     OAuth2Config_AuthType_URL_ENCODED_BODY,
                 0, false, false, false, false, false, DisabledSameSite, DisabledSameSite,
                 DisabledSameSite, DisabledSameSite, DisabledSameSite, DisabledSameSite,
                 DisabledSameSite, 0, 0, true /* disable_token_encryption */));
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Cookie.get(), "other=1"},
      {Http::Headers::get().Cookie.get(), "RefreshToken_0=plain-"},
      {Http::Headers::get().Cookie.get(), "RefreshToken_1=refresh-token"},
      {Http::Headers::get().Cookie.get(), "RefreshToken_chunks=2"},
      {Http::Headers::get().Cookie.get(), "BearerToken_0=partial"},
      {Http::Headers::get().Cookie.get(), "BearerToken_chunks=2"},
  };
  decryptAndUpdateOAuthTokenCookiesForTest(request_headers);

  const auto cookies = Http::Utility::parseCookies(request_headers);
  EXPECT_EQ(cookies.at("RefreshToken"), "plain-refresh-token");
  EXPECT_EQ(cookies.at("other"), "1");
  for (const absl::string_view name : {"RefreshToken_0", "RefreshToken_1", "RefreshToken_chunks",
                                       "BearerToken", "BearerToken_0", "BearerToken_chunks"}) {
    EXPECT_FALSE(cookies.contains(name)) << name;
  }
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_reassembled_.value());
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_malformed_chunks_.value());
}

// A token is accepted up to exactly MaxChunksPerToken chunks and refused one byte past that.
// Encryption is off so the cookie value is the token itself and the math is exact.
TEST_F(OAuth2Test, TokenCookieChunkLimitBoundary) {
  constexpr auto DisabledSameSite = ::envoy::extensions::filters::http::oauth2::v3::
      CookieConfig_SameSite::CookieConfig_SameSite_DISABLED;
  const auto make_config = [&]() {
    return getConfig(true /* forward_bearer_token */, false /* use_refresh_token */,
                     ::envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
                         OAuth2Config_AuthType_URL_ENCODED_BODY,
                     0, false, false, false, false, false, DisabledSameSite, DisabledSameSite,
                     DisabledSameSite, DisabledSameSite, DisabledSameSite, DisabledSameSite,
                     DisabledSameSite, 0, 0, true /* disable_token_encryption */);
  };

  // Each chunk carries "BearerToken_<index>=" and the tail, and the data fills the rest.
  init(make_config());
  setCookieExpiresForTest("600", "600", "600");
  const size_t chunk_size =
      MaxCookieSize - (std::string("BearerToken").size() + 3 /* "_<index>" */ + 1 /* "=" */ +
                       cookieTailSizeForTest(1));

  const auto login = [&](size_t token_size) {
    init(make_config());
    test_time_.setSystemTime(SystemTime(std::chrono::seconds(1000)));
    Http::TestRequestHeaderMapImpl request_headers{
        {Http::Headers::get().Path.get(), "/original_path"},
        {Http::Headers::get().Host.get(), "traffic.example.com"},
        {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
        {Http::Headers::get().Scheme.get(), "https"},
    };
    std::string legit_token{"legit_token"};
    EXPECT_CALL(*validator_, setParams(_, _));
    EXPECT_CALL(*validator_, isValid()).WillOnce(Return(true));
    EXPECT_CALL(*validator_, token()).WillRepeatedly(ReturnRef(legit_token));
    filter_->decodeHeaders(request_headers, false);
    filter_->onGetAccessTokenSuccess(std::string(token_size, 'a'), "", "",
                                     std::chrono::seconds(600));
  };

  // Exactly MaxChunksPerToken full chunks is accepted.
  std::string chunk_count;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&chunk_count](Http::ResponseHeaderMap& headers, bool) {
        chunk_count = Http::Utility::parseSetCookieValue(headers, "BearerToken_chunks");
      }));
  login(MaxChunksPerToken * chunk_size);
  EXPECT_EQ(chunk_count, absl::StrCat(MaxChunksPerToken));
  EXPECT_EQ(0, config_->stats().oauth_token_cookie_oversized_.value());

  // One more byte would need one more chunk, so the session is refused.
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::ServiceUnavailable, "Service Unavailable", _, _, _));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_TRUE(headers.get(Http::Headers::get().SetCookie).empty());
      }));
  login(MaxChunksPerToken * chunk_size + 1);
  EXPECT_EQ(1, config_->stats().oauth_token_cookie_oversized_.value());
}

// Chunk deletes carry the same attributes as the single cookie delete: the cookie domain, and the
// Secure attribute for a "__Secure-" name, without which the browser ignores the delete.
TEST_F(OAuth2Test, ChunkDeletesCarryDomainAndSecureAttribute) {
  envoy::extensions::filters::http::oauth2::v3::OAuth2Config p;
  auto* endpoint = p.mutable_token_endpoint();
  endpoint->set_cluster("auth.example.com");
  endpoint->set_uri("auth.example.com/_oauth");
  p.set_authorization_endpoint("https://auth.example.com/oauth/authorize/");
  p.mutable_signout_path()->mutable_path()->set_exact("/_signout");
  p.mutable_redirect_path_matcher()->mutable_path()->set_exact(TEST_CALLBACK);
  auto* credentials = p.mutable_credentials();
  credentials->set_client_id(TEST_CLIENT_ID);
  credentials->mutable_token_secret()->set_name("secret");
  credentials->mutable_hmac_secret()->set_name("hmac");
  credentials->set_cookie_domain("example.com");
  credentials->mutable_cookie_names()->set_id_token("__Secure-IdToken");
  auto secret_reader = std::make_shared<MockSecretReader>();
  init(std::make_shared<FilterConfig>(p, factory_context_.server_factory_context_, secret_reader,
                                      scope_, "test."));

  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().Path.get(), "/_signout"},
      {Http::Headers::get().Host.get(), "traffic.example.com"},
      {Http::Headers::get().Method.get(), Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Scheme.get(), "https"},
      {Http::Headers::get().Cookie.get(), "__Secure-IdToken_0=a"},
      {Http::Headers::get().Cookie.get(), "__Secure-IdToken_1=b"},
      {Http::Headers::get().Cookie.get(), "__Secure-IdToken_chunks=2"},
  };

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        std::vector<std::string> set_cookies;
        headers.iterate(
            [&set_cookies](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
              if (header.key().getStringView() == Http::Headers::get().SetCookie.get()) {
                set_cookies.push_back(std::string(header.value().getStringView()));
              }
              return Http::HeaderMap::Iterate::Continue;
            });
        for (const absl::string_view name :
             {"__Secure-IdToken_0", "__Secure-IdToken_1", "__Secure-IdToken_chunks"}) {
          EXPECT_THAT(set_cookies,
                      testing::Contains(absl::StrCat(
                          name, "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT",
                          ";domain=example.com; Secure")))
              << name;
        }
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
