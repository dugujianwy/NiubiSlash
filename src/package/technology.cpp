#include "technology.h"
#include "general.h"
#include "skill.h"
#include "standard-skillcards.h"
#include "carditem.h"
#include "engine.h"
#include "standard.h"

class Tuiyan:public TriggerSkill{
public:
    Tuiyan():TriggerSkill("tuiyan"){
        events << CardUsed << TurnStart;
    }

    virtual bool trigger(TriggerEvent event, ServerPlayer *lu, QVariant &data) const{
        if(event == TurnStart){
            lu->tag["bo"] = 0;
            lu->setMark("tuiyan", 0);
            lu->setMark("tuiyanfalse", 0);
            return false;
        }
        Room *room = lu->getRoom();
        if(room->getCurrent() != lu)
            return false;
        CardUseStruct use = data.value<CardUseStruct>();
        const Card *usecard = use.card;

        if(use.card->getId() < 0){
            if(use.card->getSubtype() == "skill_card"
               || use.card->getSubcards().length() > 1
               || use.card->getNumber() == 0)
                return false;
            usecard = Sanguosha->getCard(use.card->getSubcards().first());
        }
        int precardnum = lu->tag.value("bo").toInt();
        if(lu->getMark("tuiyanfalse") == 0 && usecard->getNumber() > precardnum){
            if(precardnum == 0 || lu->askForSkillInvoke(objectName(), data)){
                ServerPlayer *target = room->askForPlayerChosen(lu, room->getAlivePlayers(), objectName());
                use.to.clear();
                use.to << target;

                LogMessage log;
                log.type = "$Tuiyan";
                log.from = lu;
                log.to = use.to;
                log.card_str = QString::number(usecard->getId());
                room->sendLog(log);
            }
            lu->addMark("tuiyan");
        }
        else
            lu->setMark("tuiyanfalse", 1);

        lu->tag["bo"] = usecard->getNumber();
        data = QVariant::fromValue(use);
        return false;
    }
};

class Tianji: public PhaseChangeSkill{
public:
    Tianji():PhaseChangeSkill("tianji"){
        frequency = Frequent;
    }

    virtual int getPriority() const{
        return -1;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return PhaseChangeSkill::triggerable(target)
                && target->getPhase() == Player::NotActive
                && target->getMark("tuiyanfalse") == 0;
    }

    virtual bool onPhaseChange(ServerPlayer *target) const{
        Room *room = target->getRoom();
        target->setMark("mingfa", 0);
        if(target->getMark("tuiyan") >= 3 && target->askForSkillInvoke(objectName())){
            target->setMark("tuiyan", 0);
            target->setMark("mingfa", 1);
            room->getThread()->trigger(TurnStart, target);
        }
        return false;
    }
};

class Mingfa:public TriggerSkill{
public:
    Mingfa():TriggerSkill("mingfa"){
        events << TurnStart;
        frequency = Compulsory;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target->getMark("mingfa") > 0;
    }

    virtual bool trigger(TriggerEvent , ServerPlayer *lu, QVariant &data) const{
        Room *room = lu->getRoom();
        int card_id = room->drawCard();

        LogMessage log;
        log.type = "$Mingfa";
        log.from = lu;
        log.card_str = QString::number(card_id);
        room->sendLog(log);

        room->getThread()->delay();
        if(Sanguosha->getCard(card_id)->getSuit() == Card::Spade){
            log.type = "#TriggerSkill";
            log.arg = objectName();
            room->sendLog(log);

            room->loseHp(lu, 2);
        }
        room->throwCard(card_id);
        lu->setMark("mingfa", 0);
        return false;
    }
};

class Yueli:public TriggerSkill{
public:
    Yueli():TriggerSkill("yueli"){
        events << HpRecover << Predamaged << ToDrawNCards << CardDiscarded;
    }

    virtual bool trigger(TriggerEvent event, ServerPlayer *dukui, QVariant &data) const{
        Room *room = dukui->getRoom();
        LogMessage log;
        log.from = dukui;

        switch(event){
        case HpRecover:{
            RecoverStruct recover = data.value<RecoverStruct>();
            int recnum = recover.recover;
            if(dukui->getMark("m_dnc") > 0)
                return false;
            for(int i = recover.recover; i > 0; i--){
                if(dukui->askForSkillInvoke(objectName(), data)){
                    dukui->setMark("m_rec", 1);
                    dukui->drawCards(2);
                    recover.recover --;
                }
            }
            dukui->setMark("m_rec", 0);
            log.arg = QString::number(recnum - recover.recover);
            log.arg2 = QString::number((recnum - recover.recover) * 2);
            log.type = "#Yueli_rec";
            data = QVariant::fromValue(recover);
            break;
        }
        case Predamaged:{
            DamageStruct damage = data.value<DamageStruct>();
            int dmgnum = damage.damage;
            for(int i = damage.damage; i > 0; i--){
                if(dukui->getCardCount(true) > 1 && dukui->askForSkillInvoke(objectName(), data)){
                    dukui->setMark("m_pdm", 1);
                    room->askForDiscard(dukui, objectName(), 2, false, true);
                    damage.damage --;
                }
            }
            dukui->setMark("m_pdm", 0);

            log.arg = QString::number(dmgnum - damage.damage);
            log.arg2 = QString::number((dmgnum - damage.damage) * 2);
            log.type = "#Yueli_dmg";
            data = QVariant::fromValue(damage);
            break;
        }
        case ToDrawNCards:{
            int drawnum = data.toInt();
            if(dukui->getMark("m_rec") > 0)
                return false;
            while(dukui->isWounded() && drawnum >= 2 && dukui->askForSkillInvoke(objectName())){
                drawnum = drawnum - 2;
                RecoverStruct rec;
                rec.who = dukui;
                dukui->setMark("m_dnc", 1);
                room->recover(dukui, rec);
            }
            dukui->setMark("m_dnc", 0);

            log.arg = QString::number(data.toInt() - drawnum);
            log.arg2 = QString::number((data.toInt() - drawnum) / 2);
            log.type = "#Yueli_dnc";
            data = drawnum;
            break;
        }
        case CardDiscarded:{
            CardStar card = data.value<CardStar>();
            if(dukui->getMark("m_pdm") > 0)
                return false;
            QList<int> cards = card->getSubcards();
            room->fillAG(cards, dukui);
            int oldhp = dukui->getHp();
            while(cards.length() > 1 && dukui->askForSkillInvoke(objectName(), data)){
                room->loseHp(dukui);
                int card_id = room->askForAG(dukui, cards, false, objectName());
                cards.removeOne(card_id);
                room->takeAG(dukui, card_id);

                card_id = room->askForAG(dukui, cards, false, objectName());
                cards.removeOne(card_id);
                room->takeAG(dukui, card_id);
            }
            dukui->invoke("clearAG");

            log.arg = QString::number(oldhp - dukui->getHp());
            log.arg2 = QString::number((oldhp - dukui->getHp()) * 2);
            log.type = "#Yueli_cd";
            break;
        }
        default:
            return false;
        }

        if(log.arg != "0" && log.arg2 != "0")
            room->sendLog(log);
        return false;
    }
};


class Mengjie: public TriggerSkill{
public:
    Mengjie():TriggerSkill("mengjie"){
        events << CardLost;
        frequency = Frequent;
    }
    virtual bool trigger(TriggerEvent, ServerPlayer *xuan, QVariant &data) const{
        Room *room = xuan->getRoom();
        if(room->getCurrent() == xuan)
            return false;
        CardMoveStar move = data.value<CardMoveStar>();
        if(move->from_place == Player::Hand && move->to != move->from && room->askForSkillInvoke(xuan, objectName())){
             ServerPlayer *target = room->askForPlayerChosen(xuan, room->getAlivePlayers(), objectName());
             if(target){
                 target->setChained(true);
                 room->broadcastProperty(target, "chained");
             }
        }
        return false;
    }
};

class MengJie:public TriggerSkill{
public:
    MengJie():TriggerSkill("mengJie"){
        events << CardUsed << CardFinished;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return true;
    }

    virtual bool trigger(TriggerEvent event, ServerPlayer *player, QVariant &data) const{
        Room *room = player->getRoom();
        if(event == CardFinished){
            foreach(ServerPlayer *tmp, room->getAlivePlayers()){
                if(tmp->isChained() && tmp->getMark("meng") > 0){
                    tmp->setChained(false);
                    room->broadcastProperty(tmp, "chained");
                    tmp->setMark("meng", 0);
                }
            }
            return false;
        }

        ServerPlayer *zhou = room->findPlayerBySkillName(objectName());
        if(!zhou)
            return false;

        CardUseStruct use = data.value<CardUseStruct>();
        const Card *card = use.card;
        if(!((card->inherits("BasicCard") || card->isNDTrick())
            && !card->inherits("Nullification")))
            return false;

        bool enable = false;
        if(!use.to.isEmpty()){
            foreach(ServerPlayer *tmp, use.to){
                if(tmp->isChained() || tmp->hasSkill(objectName())){
                    enable = true;
                    break;
                }
            }
        }
        else if(use.from == zhou || use.from->isChained()){
            foreach(ServerPlayer *tmp, room->getAlivePlayers()){
                if(tmp->isChained()){
                    enable = true;
                    break;
                }
            }
        }

        if(enable && room->askForSkillInvoke(zhou, objectName())){
            ServerPlayer *tmp;
            int count = 10000;
            while(use.to.length() != count){
                count = use.to.length();
                //to me or my card(to me)
                if(use.to.contains(zhou) || (use.to.isEmpty() && use.from == zhou))
                    foreach(tmp, room->getAlivePlayers())
                        if(tmp->isChained() && !use.to.contains(tmp))
                            use.to << tmp;
                //to other chained player
                if(!use.to.isEmpty())
                    foreach(tmp, use.to)
                        if(tmp->isChained() && !use.to.contains(zhou)){
                            use.to << zhou;
                            break;
                        }
                //chaned player use no point card like peach
                if(use.to.isEmpty() && use.from->isChained())
                    use.to << zhou;
            }

            if(!use.to.isEmpty())
                foreach(tmp, use.to)
                    tmp->setMark("meng", 1);

            data = QVariant::fromValue(use);
        }
        return false;
    }
};

class Bianxiang: public PhaseChangeSkill{
public:
    Bianxiang():PhaseChangeSkill("bianxiang"){
        frequency = Frequent;
    }

    virtual int getPriority() const{
        return 3;
    }

    virtual bool onPhaseChange(ServerPlayer *target) const{
        Room *room = target->getRoom();
        switch(target->getPhase()){
        case Player::Start:{
            room->detachSkillFromPlayer(target, "jushou");
            target->loseSkill("jushou");
            if(target->askForSkillInvoke(objectName()))
                room->acquireSkill(target, "yinghun");
            break;
        }
        case Player::Judge:{
            room->detachSkillFromPlayer(target, "yinghun");
            target->loseSkill("yinghun");
            if(target->askForSkillInvoke(objectName()))
                room->acquireSkill(target, "guicai");
            break;
        }
        case Player::Draw:{
            room->detachSkillFromPlayer(target, "guicai");
            target->loseSkill("guicai");
            if(target->isKongcheng() && target->askForSkillInvoke(objectName()))
                room->acquireSkill(target, "yingzi");
            break;
        }
        case Player::Play:{
            room->detachSkillFromPlayer(target, "yingzi");
            target->loseSkill("yingzi");
            if(!target->isWounded() && target->askForSkillInvoke(objectName()))
                room->acquireSkill(target, "tiaoxin");
            break;
        }
        case Player::Discard:{
            room->detachSkillFromPlayer(target, "tiaoxin");
            target->loseSkill("tiaoxin");
            if(target->askForSkillInvoke(objectName())){
                if(target->getHandcardNum() >= target->getHp())
                    room->acquireSkill(target, "qinyin");
                else
                    room->acquireSkill(target, "jushou");
            }
            break;
        }
        case Player::Finish:{
            room->detachSkillFromPlayer(target, "qinyin");
            target->loseSkill("qinyin");
            //if(!target->hasSkill("jushou"))
            //    room->acquireSkill(target, "jushou");
            break;
        }
        default:
            break;
        }

        return false;
    }
};

TechnologyPackage::TechnologyPackage()
    :Package("technology")
{

    General *guanlu = new General(this, "guanlu", "god", 3);
    guanlu->addSkill(new Tuiyan);
    guanlu->addSkill(new Tianji);
    guanlu->addSkill(new Mingfa);

    General *dukui = new General(this, "dukui", "god");
    dukui->addSkill(new Yueli);

    General *zhouxuan = new General(this, "zhouxuan", "god", 3);
    zhouxuan->addSkill(new Mengjie);
    zhouxuan->addSkill(new MengJie);

    General *zhujianping = new General(this, "zhujianping", "god", 3);
    zhujianping->addSkill(new Bianxiang);
}

ADD_PACKAGE(Technology);
