#ifndef MODELREADER_HPP
#define MODELREADER_HPP

#include <QtCore/QPoint>
#include <QtCore/QString>
#include <yaml-cpp/yaml.h>

class BuildingInformation;
class CharacterInformation;
class Conf;
class ItemInformation;
class NatureElementInformation;

class ModelReader
{
    private:
        const Conf& conf;
        // Port note (EwokOS): stored by value, not by reference.  getSubModel()
        // builds a ModelReader from `{conf, key, node[key]}` where `key` is a
        // const char* (a temporary QString is materialized) and `node[key]` is a
        // temporary Node.  A temporary bound to a reference *member* in a
        // constructor's init list is destroyed when that constructor exits, so
        // the returned ModelReader would hold dangling references.  Owning the
        // QString and the (cheap, shared-data) Node by value removes the hazard.
        QString key;
        YAML::Node node;

    public:
        ModelReader(const Conf& conf, const QString& key, const YAML::Node& node);

        const QString& getKey() const;
        const YAML::Node& getNode() const;
        const ModelReader getSubModel(const char key[]) const;

        bool has(const char key[]) const;

        bool getOptionalBool(const char key[], const bool defaultValue) const;

        int getInt(const char key[]) const;
        int getOptionalInt(const char key[], const int defaultValue) const;

        qreal getReal(const char key[]) const;

        QString getString(const char key[]) const;
        QString getOptionalString(const char key[], const QString& defaultValue) const;

        QPoint getPoint(const char key[]) const;
        QPoint getOptionalPoint(const char key[], const QPoint& defaultValue) const;
        QList<QPoint> getPointList(const char key[]) const;
        QList<QPoint> getOptionalPointList(const char key[]) const;

        const BuildingInformation& getBuildingConf(const char key[]) const;

        const CharacterInformation& getCharacterConf(const char key[]) const;
        const CharacterInformation& getOptionalCharacterConf(const char key[], const CharacterInformation& defaultValue) const;
        const CharacterInformation& getOptionalCharacterConf(const char key[], const QString defaultValue) const;

        const ItemInformation& getItemConf(const char key[]) const;
        QList<const ItemInformation*> getListOfItemConfs(const char key[]) const;

        const NatureElementInformation& getNatureElementConf(const char key[]) const;

    private:
        const QString generateErrorMessage(const char key[], const char expected[]) const;
};

#endif // MODELREADER_HPP
