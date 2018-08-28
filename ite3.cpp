/**
*                         ______ _______ ______ 
*                        |_   _|__   __|  ____|
*                          | |    | |  | |__   
*                          | |    | |  |  __|  
*                         _| |_   | |  | |____ 
*                        |_____|  |_|  |______|
*                    
*
*        _______ _____ ____   _____ _____     _______ ___  _    _ _      
*      |__   __|__  _/ __ \ / ____|_   _|   |__   __|__ \ | |  | | |     
*          | |    | || |  | | |      | |        | |     ) | |  | | |     
*          | |    | || |  | | |      | |        | |    / /| |  | | |     
*          | |   _| || |__| | |____ _| |_       | |   / /_| |__| | |____ 
*          |_|  |_____\____/ \_____|_____|      |_|  |____|\____/|______|
*                                                                  
*
*          
*         Mirror, mirror on the wall, who's the hottest of them all ?
*
*
*    _                         _   _                      _                  _   
*  (_)                       | | | |                    | |                | |  
*    _ _ __    _ __ ___   __ _| |_| |__   __      _____  | |_ _ __ _   _ ___| |_ 
*  | | '_ \  | '_ ` _ \ / _` | __| '_ \  \ \ /\ / / _ \ | __| '__| | | / __| __|
*  | | | | | | | | | | | (_| | |_| | | |  \ V  V /  __/ | |_| |  | |_| \__ \ |_ 
*  |_|_| |_| |_| |_| |_|\__,_|\__|_| |_|   \_/\_/ \___|  \__|_|   \__,_|___/\__|
*
*
*/
#include <eosiolib/currency.hpp>
#include <math.h>
#include <eosiolib/crypto.h>
#include <eosiolib/eosio.hpp>
#include <eosiolib/transaction.hpp>

#define ITE S(4, ITE)
#define SATOSHI S(4, SATOSHI)
#define GAME_SYMBOL S(4, EOS)
#define FEE_ACCOUNT N(itedecompany)
#define DEV_FEE_ACCOUNT N(itedeveloper)
#define TOKEN_CONTRACT N(eosio.token)

typedef double real_type;

using namespace eosio;
using namespace std;

class ite3 : public contract
{
public:
  // parameters
  const uint64_t init_base_balance = 500 * 10000ll;         // ITE token 总发行量 500万。
  const uint64_t init_quote_balance = 50 * 10000 * 10000ll; // 初始保证金 50 万 EOS。

  const uint64_t action_limit_time = 15;       // 操作冷却时间(s)
  const uint64_t max_operate_amount_ratio = 1; // 单笔可以买入的token数量
  const uint64_t game_start_time = 1535025600; // 项目启动时间 2018-08-23 20:00:00

  const uint64_t eco_fund_ratio = 10; // 每一笔投资，直接转入到 生态基金池 的比例

  const uint64_t pos_min_staked_time = 7 * 21 * 3600;     // 最小 持币时长 需大于此时长 才能参与分红
  const uint64_t profit_sharing_period = 7 * 24 * 3600;   // 每期分红的时间间隔: 7天
  const uint64_t profit_sharing_duration = 7 * 24 * 3600; // 每期分红的分红持续时间: 7天

  const uint64_t reset_price_limit_period = 6 * 3600; // 每6个小时，重新设置token价格的涨跌停线
  const uint64_t token_price_increase_limit = 15;     // 最大涨幅，达到最大涨幅，暂时限制买入
  const uint64_t token_price_decrease_limit = 15;     // 最大跌幅，达到最大跌幅，暂时限制卖出

  const uint64_t max_holding_lock = 15; // token总销售数 达到这个比例时。解锁个人持仓限制。
  const uint64_t first_sell_lock_ = 15; // token总出售数 达到这个比例之前。解锁token卖出限制。可以开始出售token。（未启用）

  ite3(account_name self)
      : contract(self),
        _global(_self, _self),
        _shares(_self, _self),
        _players(_self, _self),
        _market(_self, _self)
  {
    // Create a new global if not exists
    auto gl_itr = _global.begin();
    if (gl_itr == _global.end())
    {
      gl_itr = _global.emplace(_self, [&](auto &gl) {
        gl.next_profit_sharing_time = now() + profit_sharing_period;
        gl.init_max = init_base_balance;
        gl.quote_balance.amount = init_quote_balance;
        gl.init_quote_balance.amount = init_quote_balance;
      });
    }

    // Create a new market if not exists
    auto market_itr = _market.begin();
    if (market_itr == _market.end())
    {
      market_itr = _market.emplace(_self, [&](auto &m) {
        m.supply.amount = 100000000000000ll;
        m.supply.symbol = SATOSHI;
        m.base.balance.amount = init_base_balance;
        m.base.balance.symbol = ITE;
        m.quote.balance.amount = init_quote_balance;
        m.quote.balance.symbol = GAME_SYMBOL;
      });
    }

    // Create default referrer account if not exists
    user_resources_table default_ref_userres(_self, DEV_FEE_ACCOUNT);
    auto default_ref_res_itr = default_ref_userres.begin();

    if (default_ref_res_itr == default_ref_userres.end())
    {
      // add new player into players table
      uint64_t player_id = _players.available_primary_key();

      _players.emplace(_self, [&](auto &new_player) {
        new_player.id = player_id;
        new_player.player_account = DEV_FEE_ACCOUNT;
      });

      default_ref_res_itr = default_ref_userres.emplace(_self, [&](auto &res) {
        res.id = player_id;
        res.referrer = DEV_FEE_ACCOUNT;
        res.owner = DEV_FEE_ACCOUNT;
        res.staked_time = now() + pos_min_staked_time;
      });
    }
    
  }

  void transfer(account_name from, account_name to, asset quantity, string memo)
  {
    if (from == _self || to != _self)
    {
      return;
    }

    eosio_assert(now() > game_start_time + random_offset(from), "The ITE Token sell will start at 2018-08-23T20:00:00");

    eosio_assert(quantity.is_valid(), "Invalid token transfer");
    eosio_assert(quantity.amount > 0, "Quantity must be positive");

    // only accepts GAME_SYMBOL for buy
    if (quantity.symbol == GAME_SYMBOL)
    {
      buy(from, quantity, memo);
    }
  }

  int64_t random_offset(account_name from)
  {
    checksum256 result;
    auto mixedBlock = tapos_block_prefix() * tapos_block_num() + from;

    const char *mixedChar = reinterpret_cast<const char *>(&mixedBlock);
    sha256((char *)mixedChar, sizeof(mixedChar), &result);
    const char *p64 = reinterpret_cast<const char *>(&result);
    auto x = 3, y = 50, z = 10;
    return (abs((int64_t)p64[x]) % (y)) + z;
  }

  void buy(account_name account, asset quant, string memo)
  {
    require_auth(account);
    eosio_assert(quant.amount > 0, "must purchase a positive amount");

    auto gl_itr = _global.begin();
    auto market_itr = _market.begin();

    auto fee = quant;
    fee.amount = (fee.amount + 199) / 200; /// .5% fee (round up)
    auto action_total_fee = fee;

    auto quant_after_fee = quant;
    quant_after_fee.amount -= fee.amount;

    // eco fund
    auto eco_fund = quant_after_fee;
    eco_fund.amount = quant_after_fee.amount * eco_fund_ratio / 100;

    quant_after_fee.amount -= eco_fund.amount;

    if (fee.amount > 0)
    {
      auto dev_fee = fee;
      dev_fee.amount = fee.amount * 30 / 100;
      fee.amount -= dev_fee.amount;

      action(
          permission_level{_self, N(active)},
          TOKEN_CONTRACT, N(transfer),
          make_tuple(_self, FEE_ACCOUNT, fee, string("buy fee")))
          .send();

      if (dev_fee.amount > 0)
      {
        action(
            permission_level{_self, N(active)},
            TOKEN_CONTRACT, N(transfer),
            make_tuple(_self, DEV_FEE_ACCOUNT, dev_fee, string("dev fee")))
            .send();
      }
    }

    int64_t ite_out;

    _market.modify(market_itr, 0, [&](auto &es) {
      ite_out = es.convert(quant_after_fee, ITE).amount;
    });

    eosio_assert(ite_out > 0, "must reserve a positive amount");

    // max operate amount limit
    auto max_operate_amount = gl_itr->init_max / 100 * max_operate_amount_ratio;
    eosio_assert(ite_out < max_operate_amount, "must reserve less than max operate amount:");

    _global.modify(gl_itr, 0, [&](auto &gl) {
      gl.counter++;
      gl.total_reserved += ite_out;
      gl.quote_balance += quant_after_fee;
      gl.eco_fund_balance += eco_fund;
    });

    auto share_ite_out = ite_out;
    auto ite_out_after_share = ite_out;
    share_ite_out = ite_out / 100;
    ite_out_after_share -= share_ite_out;

    user_resources_table userres(_self, account);
    auto res_itr = userres.begin();

    if (res_itr == userres.end())
    {
      //get referrer
      account_name referrer = string_to_name(memo.c_str());

      user_resources_table ref_userres(_self, referrer);

      auto ref_res_itr = ref_userres.begin();

      if (ref_res_itr == ref_userres.end())
      {
        referrer = DEV_FEE_ACCOUNT;
      }

      // add new player into players table
      uint64_t player_id = _players.available_primary_key();

      _players.emplace(_self, [&](auto &new_player) {
        new_player.id = player_id;
        new_player.player_account = account;
      });

      res_itr = userres.emplace(account, [&](auto &res) {
        res.id = player_id;
        res.referrer = referrer;
        res.owner = account;
        res.hodl = ite_out_after_share;
        res.action_count++;
        res.fee_amount += action_total_fee;
        res.out += quant;
        res.staked_time = now() + pos_min_staked_time;
      });

      if (referrer == DEV_FEE_ACCOUNT)
      {
        user_resources_table dev_ref_userres(_self, referrer);
        auto dev_ref_res_itr = dev_ref_userres.begin();

        dev_ref_userres.modify(dev_ref_res_itr, account, [&](auto &res) {
          res.hodl += share_ite_out;
          res.total_share_ite += share_ite_out;
          res.ref_count++;
        });
      }
      else
      {
        ref_userres.modify(ref_res_itr, account, [&](auto &res) {
          res.hodl += share_ite_out;
          res.total_share_ite += share_ite_out;
          res.ref_count++;
        });
      }
    }
    else
    {
      // time limit
      auto time_diff = now() - res_itr->last_action_time;
      eosio_assert(time_diff > action_limit_time, "please wait a moment");

      // max hold limit
      auto max_holding_lock_line = gl_itr->init_max / 100 * max_holding_lock;
      if (gl_itr->total_reserved < max_holding_lock_line)
      {
        eosio_assert((res_itr->hodl + ite_out) <= max_operate_amount, "can not hold more than 1% before 15%");
      }

      userres.modify(res_itr, account, [&](auto &res) {
        res.hodl += ite_out_after_share;
        res.last_action_time = now();
        res.staked_time = now() + pos_min_staked_time;
        res.action_count++;
        res.fee_amount += action_total_fee;
        res.out += quant;
      });

      if (share_ite_out > 0)
      {
        user_resources_table ref_userres(_self, res_itr->referrer);

        auto ref_res_itr = ref_userres.begin();

        ref_userres.modify(ref_res_itr, account, [&](auto &res) {
          res.hodl += share_ite_out;
          res.total_share_ite += share_ite_out;
        });
      }
    }

    trigger_profit_sharing();
  }

  void sell(account_name account, int64_t amount)
  {
    require_auth(account);
    eosio_assert(amount > 0, "cannot sell negative amount");

    auto gl_itr = _global.begin();

    user_resources_table userres(_self, account);
    auto res_itr = userres.begin();

    eosio_assert(res_itr != userres.end(), "no resource row");
    eosio_assert(res_itr->hodl >= amount, "insufficient quota");

    // time limit
    auto time_diff = now() - res_itr->last_action_time;
    eosio_assert(time_diff > action_limit_time, "please wait a moment");

    // max operate amount limit
    auto max_operate_amount = gl_itr->init_max / 100 * max_operate_amount_ratio;
    eosio_assert(amount < max_operate_amount, "must sell less than max operate amount");

    asset tokens_out;

    auto itr = _market.begin();

    _market.modify(itr, 0, [&](auto &es) {
      tokens_out = es.convert(asset(amount, ITE), GAME_SYMBOL);
    });

    eosio_assert(tokens_out.amount > 0, "must payout a positive amount");

    auto max = gl_itr->quote_balance - gl_itr->init_quote_balance;

    if (tokens_out > max)
    {
      tokens_out = max;
    }

    _global.modify(gl_itr, 0, [&](auto &gl) {
      gl.counter++;
      gl.total_reserved -= amount;
      gl.quote_balance -= tokens_out;
    });

    auto fee = (tokens_out.amount + 199) / 200; /// .5% fee (round up)
    auto action_total_fee = fee;

    auto quant_after_fee = tokens_out;
    quant_after_fee.amount -= fee;

    userres.modify(res_itr, account, [&](auto &res) {
      res.hodl -= amount;
      res.last_action_time = now();
      res.action_count++;
      res.fee_amount += asset(action_total_fee, GAME_SYMBOL);
      res.in += tokens_out;
    });

    action(
        permission_level{_self, N(active)},
        TOKEN_CONTRACT, N(transfer),
        make_tuple(_self, account, quant_after_fee, string("sell payout")))
        .send();

    if (fee > 0)
    {
      auto dev_fee = fee;
      dev_fee = fee * 30 / 100;
      fee -= dev_fee;

      action(
          permission_level{_self, N(active)},
          TOKEN_CONTRACT, N(transfer),
          make_tuple(_self, FEE_ACCOUNT, asset(fee, GAME_SYMBOL), string("sell fee")))
          .send();

      if (dev_fee > 0)
      {
        action(
            permission_level{_self, N(active)},
            TOKEN_CONTRACT, N(transfer),
            make_tuple(_self, DEV_FEE_ACCOUNT, asset(dev_fee, GAME_SYMBOL), string("dev fee")))
            .send();
      }
    }

    trigger_profit_sharing();
  }

  /**
  * 触发系统保护开关、状态切换。
  * 涨跌停开关
  * 熔断开关 
  */
  void trigger_system_protection()
  {
    // TODO
  }

  /*
  * 分红，是拿出生态基金池 中的EOS，对token持有者进行分红。持有越多的token, 将获得越多的EOS分红。
  * 这是一种类似 POS 的分配方式，所以，我们定义了一个 ”持有时间“ 的概念。 当账户满足 最小 ”持有时间“的时候，才能参与分红。
  * 在持有时间内，如果进行任意数量的买入操作，将重新计算 ”持有时间“。 这是为了防止有人在 分红期间，大量买入，拿到分红以后，立刻大量卖出。
  * 
  * 为了将去中心化进行到底。我们设计了 “投票发起分红” 的机制。 
  * 在系统启动的七天后，系统将自动启动投票， 当投票总数，超过当前售出token总量的15%。 
  * 则进入为期三天的分红周期。三天后，本期分红结束，系统进入7天的分红冷却时间
  * 当期分红结束以后，未分红奖励，进入下一轮
  */
  void trigger_profit_sharing_vote()
  {
    // TODO
  }

  /**
  * 由系统定时自动发起分红的方案
  */
  void trigger_profit_sharing()
  {
    auto gl_itr = _global.begin();

    // 检测是否到了分红时间
    if (now() > gl_itr->next_profit_sharing_time)
    {
      auto max_share = gl_itr->eco_fund_balance;

      // 每周分红一次，每次只分红基金池中的10分之一。保证可持续性分红。
      max_share.amount = max_share.amount / 10;

      auto eos_per_ite = max_share;

      if (gl_itr->total_reserved > 0)
      {
        eos_per_ite.amount = max_share.amount / gl_itr->total_reserved;
      }

      // 只有 eos_per_ite > 0 才发起分红
      if (eos_per_ite.amount > 0)
      {
        // create a new profit sharing record
        _shares.emplace(_self, [&](auto &ps) {
          ps.id = _shares.available_primary_key();
          ps.eco_fund_balance_snapshoot = gl_itr->eco_fund_balance;
          ps.quote_balance_snapshoot = gl_itr->quote_balance;
          ps.reserved_snapshoot = gl_itr->total_reserved;
          ps.total_share_balance = max_share;
          ps.eos_per_ite = eos_per_ite;
          ps.start_time = now();
          ps.end_time = now() + profit_sharing_duration;
        });

        // 设置下一次分红时间
        _global.modify(gl_itr, 0, [&](auto &gl) {
          gl.next_profit_sharing_time = now() + profit_sharing_period;
        });
      }
    }
  }

  void claim(account_name account, int64_t shareid)
  {
    require_auth(account);

    auto ps_itr = _shares.find(shareid);

    eosio_assert(ps_itr != _shares.end(), "profit share no found");
    eosio_assert(now() < ps_itr->end_time, "the profit share has expired");

    user_resources_table userres(_self, account);
    auto res_itr = userres.begin();

    eosio_assert(res_itr != userres.end(), "sorry, you are not an ITE user");

    claims_index _user_claims(_self, account);
    auto claim_itr = _user_claims.find(ps_itr->id);
    eosio_assert(claim_itr == _user_claims.end(), "you had claim this profit share");

    eosio_assert(now() > res_itr->staked_time, "you can't get this profit share now because of token staked time");
    eosio_assert(now() > res_itr->next_claim_time, "in claim time cooldown");

    auto reward = ps_itr->eos_per_ite;
    reward.amount = reward.amount * res_itr->hodl;

    userres.modify(res_itr, account, [&](auto &res) {
      res.claim_count++;
      res.claim += reward;
      res.last_claim_time = now();
      res.next_claim_time = now() + profit_sharing_duration;
    });

    // create new user claim record
    _user_claims.emplace(_self, [&](auto &new_claim) {
      new_claim.share_id = ps_itr->id;
      new_claim.my_ite_snapshoot = res_itr->hodl;
      new_claim.reward = reward;
    });

    if (reward.amount > 0)
    {
      action(
          permission_level{_self, N(active)},
          TOKEN_CONTRACT, N(transfer),
          make_tuple(_self, account, reward, string("claim profit sharing reward")))
          .send();

      auto gl_itr = _global.begin();
      _global.modify(gl_itr, 0, [&](auto &gl) {
        gl.eco_fund_balance -= reward;
      });
    }
  }

private:
  // @abi table global i64
  struct global
  {
    uint64_t id = 0;

    uint64_t counter;
    uint64_t init_max;
    uint64_t total_reserved;
    uint64_t start_time = now();
    uint64_t next_profit_sharing_time;

    asset quote_balance = asset(0, GAME_SYMBOL);
    asset init_quote_balance = asset(0, GAME_SYMBOL);
    asset eco_fund_balance = asset(0, GAME_SYMBOL);

    uint64_t primary_key() const { return id; }

    EOSLIB_SERIALIZE(global, (id)(counter)(init_max)(total_reserved)(start_time)(next_profit_sharing_time)(quote_balance)(init_quote_balance)(eco_fund_balance))
  };

  typedef eosio::multi_index<N(global), global> global_index;
  global_index _global;

  // @abi table shares i64
  struct shares
  {
    uint64_t id;

    asset eco_fund_balance_snapshoot = asset(0, GAME_SYMBOL); // 生态基金池快照
    asset quote_balance_snapshoot = asset(0, GAME_SYMBOL);    // 市值快照
    uint64_t reserved_snapshoot;                              // ITE Token 售出快照
    asset total_share_balance = asset(0, GAME_SYMBOL);        // 本期的分红总额
    asset eos_per_ite = asset(0, GAME_SYMBOL);                // 每个ITE可以分得多少EOS
    uint64_t start_time;                                      // 本期分红开始时间
    uint64_t end_time;                                        // 本期分红截止时间（每期持续n天）

    uint64_t primary_key() const { return id; }

    EOSLIB_SERIALIZE(shares, (id)(eco_fund_balance_snapshoot)(quote_balance_snapshoot)(reserved_snapshoot)(total_share_balance)(eos_per_ite)(start_time)(end_time))
  };

  typedef eosio::multi_index<N(shares), shares> shares_index;
  shares_index _shares;

  // @abi table claims i64
  struct claims
  {
    uint64_t share_id;
    uint64_t my_ite_snapshoot;
    uint64_t claim_date = now();
    asset reward = asset(0, GAME_SYMBOL);

    uint64_t primary_key() const { return share_id; }

    EOSLIB_SERIALIZE(claims, (share_id)(my_ite_snapshoot)(claim_date)(reward))
  };

  typedef eosio::multi_index<N(claims), claims> claims_index;

  // @abi table player i64
  struct player
  {
    int64_t id;
    account_name player_account;

    uint64_t primary_key() const { return id; }

    EOSLIB_SERIALIZE(player, (id)(player_account))
  };

  typedef eosio::multi_index<N(player), player> player_index;
  player_index _players;

  // @abi table userinfo i64
  struct userinfo
  {
    int64_t id;
    account_name owner;
    account_name referrer;                    // 推荐人
    int64_t hodl;                             // 持有智子数量
    int64_t total_share_ite;                  // 累计推荐奖励ITE
    int64_t ref_count;                        // 累计推荐人
    int64_t claim_count;                      // 参与领分红次数
    int64_t action_count;                     // 累计操作次数
    int64_t last_action_time = now();         // 上一次操作时间
    int64_t last_claim_time;                  // 上一次领分红时间
    int64_t next_claim_time;                  // 下一次可领分红时间
    int64_t staked_time;                      // 锁币时间
    asset fee_amount = asset(0, GAME_SYMBOL); // 累计手续费
    asset in = asset(0, GAME_SYMBOL);         // 累计收入
    asset out = asset(0, GAME_SYMBOL);        // 累计支出
    asset claim = asset(0, GAME_SYMBOL);      // 累计分红
    int64_t join_time = now();

    uint64_t primary_key() const { return id; }

    EOSLIB_SERIALIZE(userinfo, (id)(owner)(referrer)(hodl)(total_share_ite)(ref_count)(claim_count)(action_count)(last_action_time)(last_claim_time)(next_claim_time)(staked_time)(fee_amount)(in)(out)(claim)(join_time))
  };

  typedef eosio::multi_index<N(userinfo), userinfo> user_resources_table;

  /**
    *  Uses Bancor math to create a 50/50 relay between two asset types. The state of the
    *  bancor exchange is entirely contained within this struct. There are no external
    *  side effects associated with using this API.
    *  Love BM. Love Bancor.
    */
  struct exchange_state
  {
    uint64_t id = 0;

    asset supply;

    struct connector
    {
      asset balance;
      double weight = .5;

      EOSLIB_SERIALIZE(connector, (balance)(weight))
    };

    connector base;
    connector quote;

    uint64_t primary_key() const { return id; }

    asset convert_to_exchange(connector &c, asset in)
    {
      real_type R(supply.amount);
      real_type C(c.balance.amount + in.amount);
      real_type F(c.weight / 1000.0);
      real_type T(in.amount);
      real_type ONE(1.0);

      real_type E = -R * (ONE - pow(ONE + T / C, F));
      int64_t issued = int64_t(E);

      supply.amount += issued;
      c.balance.amount += in.amount;

      return asset(issued, supply.symbol);
    }

    asset convert_from_exchange(connector &c, asset in)
    {
      eosio_assert(in.symbol == supply.symbol, "unexpected asset symbol input");

      real_type R(supply.amount - in.amount);
      real_type C(c.balance.amount);
      real_type F(1000.0 / c.weight);
      real_type E(in.amount);
      real_type ONE(1.0);

      real_type T = C * (pow(ONE + E / R, F) - ONE);
      int64_t out = int64_t(T);

      supply.amount -= in.amount;
      c.balance.amount -= out;

      return asset(out, c.balance.symbol);
    }

    asset convert(asset from, symbol_type to)
    {
      auto sell_symbol = from.symbol;
      auto ex_symbol = supply.symbol;
      auto base_symbol = base.balance.symbol;
      auto quote_symbol = quote.balance.symbol;

      if (sell_symbol != ex_symbol)
      {
        if (sell_symbol == base_symbol)
        {
          from = convert_to_exchange(base, from);
        }
        else if (sell_symbol == quote_symbol)
        {
          from = convert_to_exchange(quote, from);
        }
        else
        {
          eosio_assert(false, "invalid sell");
        }
      }
      else
      {
        if (to == base_symbol)
        {
          from = convert_from_exchange(base, from);
        }
        else if (to == quote_symbol)
        {
          from = convert_from_exchange(quote, from);
        }
        else
        {
          eosio_assert(false, "invalid conversion");
        }
      }

      if (to != from.symbol)
        return convert(from, to);

      return from;
    }

    EOSLIB_SERIALIZE(exchange_state, (supply)(base)(quote))
  };

  typedef eosio::multi_index<N(market), exchange_state> market;
  market _market;
};

#define EOSIO_ABI_PRO(TYPE, MEMBERS)                                                                                      \
  extern "C" {                                                                                                            \
  void apply(uint64_t receiver, uint64_t code, uint64_t action)                                                           \
  {                                                                                                                       \
    auto self = receiver;                                                                                                 \
    if (action == N(onerror))                                                                                             \
    {                                                                                                                     \
      eosio_assert(code == N(eosio), "onerror action's are only valid from the \"eosio\" system account");                \
    }                                                                                                                     \
    if ((code == TOKEN_CONTRACT && action == N(transfer)) || (code == self && (action == N(sell) || action == N(claim)))) \
    {                                                                                                                     \
      TYPE thiscontract(self);                                                                                            \
      switch (action)                                                                                                     \
      {                                                                                                                   \
        EOSIO_API(TYPE, MEMBERS)                                                                                          \
      }                                                                                                                   \
    }                                                                                                                     \
  }                                                                                                                       \
  }

EOSIO_ABI_PRO(ite3, (transfer)(sell)(claim))

// EOSIO_ABI(ite3, (transfer)(sell)(claim))